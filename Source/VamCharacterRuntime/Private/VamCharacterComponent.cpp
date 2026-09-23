#include "VamCharacterComponent.h"
#include "VamCharacterDefinition.h"
#include "VamShapeAnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/Skeleton.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"

void UVamCharacterComponent::BeginPlay() { Super::BeginPlay(); LoadCharacter(); }
void UVamCharacterComponent::EndPlay(const EEndPlayReason::Type Reason) { UnloadCharacter(); Super::EndPlay(Reason); }

void UVamCharacterComponent::UnloadCharacter()
{
    ++Generation;
    if (Pending) { Pending->CancelHandle(); Pending.Reset(); }
    for (auto Part : LoadedParts) if (Part) Part->DestroyComponent();
    LoadedParts.Reset();
    if (Body) Body->DestroyComponent();
    Body = nullptr;
    LoadedDefinition = nullptr;
    PreviewState = FVamShapeState(); CommittedState = FVamShapeState(); ShapeReferencePose.Reset();
}

void UVamCharacterComponent::LoadCharacter()
{
    UnloadCharacter();
    if (Definition.IsNull()) { OnLoaded.Broadcast(false, TEXT("No CharacterDefinition assigned")); return; }
    const uint64 Ticket = Generation;
    Pending = UAssetManager::GetStreamableManager().RequestAsyncLoad(Definition.ToSoftObjectPath(),
        FStreamableDelegate::CreateWeakLambda(this, [this, Ticket]() { LoadMeshes(Ticket); }));
}

void UVamCharacterComponent::LoadMeshes(uint64 Ticket)
{
    if (Ticket != Generation) return;
    LoadedDefinition = Definition.Get();
    if (!LoadedDefinition || !LoadedDefinition->bBuildVerified ||
        (LoadedDefinition->ShapeConvention != TEXT("neutral_plus_parameters") &&
         LoadedDefinition->ShapeConvention != TEXT("appearance_plus_parameter_offsets")) || LoadedDefinition->Body.IsNull())
    { OnLoaded.Broadcast(false, TEXT("Definition missing or source build not verified")); return; }
    TArray<FSoftObjectPath> Paths { LoadedDefinition->Body.ToSoftObjectPath(), LoadedDefinition->Skeleton.ToSoftObjectPath() };
    if (!LoadedDefinition->Shape.IsNull()) Paths.AddUnique(LoadedDefinition->Shape.ToSoftObjectPath());
    if (!LoadedDefinition->ImportedAppearance.IsNull()) Paths.AddUnique(LoadedDefinition->ImportedAppearance.ToSoftObjectPath());
    for (const auto& Part : LoadedDefinition->Parts) if (!Part.IsNull()) Paths.AddUnique(Part.ToSoftObjectPath());
    Pending = UAssetManager::GetStreamableManager().RequestAsyncLoad(Paths,
        FStreamableDelegate::CreateWeakLambda(this, [this, Ticket]() { Assemble(Ticket); }));
}

void UVamCharacterComponent::Assemble(uint64 Ticket)
{
    if (Ticket != Generation || !LoadedDefinition) return;
    USkeletalMesh* Mesh = LoadedDefinition->Body.Get();
    if (!Mesh || !LoadedDefinition->Skeleton.Get() || Mesh->GetSkeleton() != LoadedDefinition->Skeleton.Get())
    { OnLoaded.Broadcast(false, TEXT("Body or exact skeleton dependency unavailable")); return; }
    Body = NewObject<USkeletalMeshComponent>(GetOwner(), NAME_None, RF_Transient);
    Body->SetupAttachment(this);
    Body->SetSkeletalMeshAsset(Mesh);
    Body->SetAnimInstanceClass(UVamShapeAnimInstance::StaticClass());
    Body->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
    Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Body->RegisterComponent();
    Body->SetUpdateAnimationInEditor(true);
    FString Missing;
    for (const auto& Reference : LoadedDefinition->Parts)
    {
        auto* PartMesh = Reference.Get();
        // Sharing is admitted only by the builder's exact bind signature and skeleton identity.
        bool Compatible = PartMesh && PartMesh->GetSkeleton() == Mesh->GetSkeleton();
        if (Compatible)
        {
            const auto& A = Mesh->GetRefSkeleton(); const auto& B = PartMesh->GetRefSkeleton();
            Compatible = A.GetNum() == B.GetNum();
            for (int32 Index=0; Compatible && Index<A.GetNum(); ++Index)
                Compatible = A.GetBoneName(Index)==B.GetBoneName(Index) && A.GetParentIndex(Index)==B.GetParentIndex(Index) &&
                    A.GetRefBonePose()[Index].Equals(B.GetRefBonePose()[Index], 1.e-6);
        }
        if (!Compatible) { Missing += Reference.ToString() + TEXT("; "); continue; }
        auto* Part = NewObject<USkeletalMeshComponent>(GetOwner(), NAME_None, RF_Transient);
        Part->SetupAttachment(Body);
        Part->SetSkeletalMeshAsset(PartMesh);
        Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Part->SetLeaderPoseComponent(Body);
        Part->RegisterComponent();
        LoadedParts.Add(Part);
    }
    ResetToImportedAppearance();
    CommitShape();
    UE_LOG(LogTemp, Display, TEXT("VAM_NATIVE_RUNTIME_LOADED Body=%s Parts=%d Morphs=%d Missing=%s"),
        *Mesh->GetPathName(), LoadedParts.Num(), LoadedDefinition->Parameters.Num(), *Missing);
    OnLoaded.Broadcast(true, Missing.IsEmpty() ? TEXT("Native character loaded") : TEXT("Body loaded; missing or incompatible parts: ") + Missing);
}

bool UVamCharacterComponent::SetParameter(FName Name, float Value)
{
    return PreviewParameters({{Name, Value}});
}

FVamShapeState UVamCharacterComponent::GetShapeState(bool Committed) const
{
    return Committed ? CommittedState : PreviewState;
}

FVamCharacterState UVamCharacterComponent::GetCharacterState() const
{
    FVamCharacterState State;
    State.CommittedShape=CommittedState;State.PreviewShape=PreviewState;
    if (Body) State.PoseComponentSpace=Body->GetComponentSpaceTransforms();
    return State; // Stage05 has no simulation solver; never store a posed mesh as Shape.
}

bool UVamCharacterComponent::PreviewParameters(const TMap<FName,float>& Values)
{
    if (!Body || !LoadedDefinition) return false;
    // Validate the entire batch before changing anything.
    for (const auto& Pair : Values)
        if (!FMath::IsFinite(Pair.Value) || !LoadedDefinition->Parameters.ContainsByPredicate(
            [&Pair](const FVamMorphParameter& P) { return P.Target==Pair.Key; })) return false;
    TArray<FName> Changed;
    for (const auto& Pair : Values)
    {
        const auto& P=*LoadedDefinition->Parameters.FindByPredicate([&Pair](const FVamMorphParameter& X){return X.Target==Pair.Key;});
        const float Value=FMath::Clamp(Pair.Value,P.Minimum,P.Maximum);
        const float* Old=PreviewState.Values.Find(Pair.Key);
        if (!Old || *Old!=Value) { PreviewState.Values.Add(Pair.Key,Value); Changed.Add(Pair.Key); }
    }
    if (!Changed.IsEmpty()) ApplyShape(Changed,false);
    return true;
}

void UVamCharacterComponent::ApplyShape(const TArray<FName>& Changed, bool bCommitted)
{
    if (!Body || !LoadedDefinition) return;
    auto* Shape=LoadedDefinition->Shape.Get();
    ShapeReferencePose=Shape ? Shape->NeutralLocalBind : TArray<FTransform>();
    FVamShapeChange Event;
    Event.Parameters=Changed; Event.bCommitted=bCommitted;
    for (const auto& P : LoadedDefinition->Parameters)
    {
        const float* Found=PreviewState.Values.Find(P.Target);
        const float Absolute=Found ? *Found : P.DefaultValue;
        const float Morph=Absolute-(LoadedDefinition->ShapeConvention==TEXT("appearance_plus_parameter_offsets") ? P.DefaultValue : 0.f);
        if (Body->GetSkeletalMeshAsset()->FindMorphTarget(P.Target)) Body->SetMorphTarget(P.Target,Morph);
        for (auto Part : LoadedParts)
            if (Part->GetSkeletalMeshAsset()->FindMorphTarget(P.Target)) Part->SetMorphTarget(P.Target,Morph);
        for (const auto& Offset : P.BoneCenters)
            if (ShapeReferencePose.IsValidIndex(Offset.BoneIndex))
                ShapeReferencePose[Offset.BoneIndex].AddToTranslation(Offset.LocalTranslation*Absolute);
        if (Changed.Contains(P.Target))
        {
            for (FName Region : P.AffectedRegions) Event.Regions.AddUnique(Region);
            for (const auto& Offset : P.BoneCenters)
                if (Body->GetSkeletalMeshAsset()->GetRefSkeleton().IsValidIndex(Offset.BoneIndex))
                    Event.Bones.AddUnique(Body->GetSkeletalMeshAsset()->GetRefSkeleton().GetBoneName(Offset.BoneIndex));
        }
    }
    if (ShapeReferencePose.Num()==Body->GetSkeletalMeshAsset()->GetRefSkeleton().GetRawBoneNum())
    {
        // UE computes component-private inverse bind matrices as well as the reference pose.
        // This does not mutate the shared mesh, skeleton, or another character instance.
        Body->SetRefPoseOverride(ShapeReferencePose);
        for (auto Part : LoadedParts) Part->SetRefPoseOverride(ShapeReferencePose);
        Body->InitAnim(true);
        Body->TickAnimation(0.f,false);
        Body->RefreshBoneTransforms();
    }
    // A batch can change/reset/restore several times within one game frame.
    // RecreateRenderState refreshes UE's private Morph curve cache at frame end.
    // Multiple edits coalesce to one update; final curves/binds reach every follower.
    Body->MarkRenderStateDirty();
    for (auto Part:LoadedParts) Part->MarkRenderStateDirty();
    for (const auto& Part : LoadedDefinition->Parts) Event.Parts.Add(Part.ToSoftObjectPath());
    Event.ShapeRevision=++PreviewState.Revision;
    if (bCommitted) CommittedState=PreviewState;
    OnShapeChanged.Broadcast(Event);
}

bool UVamCharacterComponent::CommitShape()
{
    if (!Body || !LoadedDefinition) return false;
    TArray<FName> Changed;
    for (const auto& Pair : PreviewState.Values)
        if (!CommittedState.Values.Contains(Pair.Key) || CommittedState.Values[Pair.Key]!=Pair.Value) Changed.Add(Pair.Key);
    ApplyShape(Changed,true);
    return true;
}

void UVamCharacterComponent::CancelShape()
{
    if (!CommittedState.Values.IsEmpty()) PreviewParameters(CommittedState.Values);
}

void UVamCharacterComponent::ResetToImportedAppearance()
{
    if (!LoadedDefinition) return;
    TMap<FName,float> Values;
    for (const auto& P : LoadedDefinition->Parameters) Values.Add(P.Target,P.DefaultValue);
    PreviewParameters(Values);
}

void UVamCharacterComponent::ResetToBaseShape()
{
    if (!LoadedDefinition) return;
    TMap<FName,float> Values;
    for (const auto& P : LoadedDefinition->Parameters) Values.Add(P.Target,0.f);
    PreviewParameters(Values);
}


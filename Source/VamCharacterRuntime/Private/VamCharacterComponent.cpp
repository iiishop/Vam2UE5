#include "VamCharacterComponent.h"
#include "VamCharacterDefinition.h"
#include "VamShapeAnimInstance.h"
#include "VamRigProfile.h"
#include "VamMotionComponent.h"
#include "VamActivePoseComponent.h"
#include "VamMaterialProfile.h"
#include "VamInteractionComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/Skeleton.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "Materials/MaterialInstanceDynamic.h"

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
    if (!RigProfile.IsNull()) Paths.AddUnique(RigProfile.ToSoftObjectPath());
    if (!AnimationClass.IsNull()) Paths.AddUnique(AnimationClass.ToSoftObjectPath());
    if (!PhysicsAsset.IsNull()) Paths.AddUnique(PhysicsAsset.ToSoftObjectPath());
    if (!AppearancePreset.IsNull()) Paths.AddUnique(AppearancePreset.ToSoftObjectPath());
    if (!MaterialProfile.IsNull()) Paths.AddUnique(MaterialProfile.ToSoftObjectPath());
    if (UVamMaterialProfile* Profile=MaterialProfile.LoadSynchronous())
    {
        for (const auto& Material:Profile->BodyMaterials)
            if (!Material.IsNull()) Paths.AddUnique(Material.ToSoftObjectPath());
        for (const auto& PartSet:Profile->PartMaterials)
            for (const auto& Material:PartSet.Materials)
                if (!Material.IsNull()) Paths.AddUnique(Material.ToSoftObjectPath());
    }
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
    if (const UVamRigProfile* Rig=RigProfile.Get())
        if (Rig->Skeleton.Get()!=Mesh->GetSkeleton())
        { OnLoaded.Broadcast(false,TEXT("Stage06 rig profile skeleton does not match body")); return; }
    if (const UPhysicsAsset* Asset=PhysicsAsset.Get())
        if (Asset->SkeletalBodySetups.IsEmpty() || Asset->ConstraintSetup.IsEmpty())
        { OnLoaded.Broadcast(false,TEXT("Stage06 physics asset has no bodies or constraints")); return; }
    Body = NewObject<USkeletalMeshComponent>(GetOwner(), NAME_None, RF_Transient);
    Body->SetupAttachment(this);
    Body->SetSkeletalMeshAsset(Mesh);
    if (UPhysicsAsset* Asset=PhysicsAsset.Get()) Body->SetPhysicsAsset(Asset);
    Body->SetAnimInstanceClass(AnimationClass.Get() ? AnimationClass.Get() : UVamShapeAnimInstance::StaticClass());
    Body->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
    Body->SetCollisionEnabled(PhysicsAsset.Get() ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
    if (PhysicsAsset.Get()) Body->SetCollisionResponseToAllChannels(ECR_Block);
    Body->RegisterComponent();
    if (const UVamMaterialProfile* Profile=MaterialProfile.Get())
        for (int32 Slot=0;Slot<Profile->BodyMaterials.Num() && Slot<Body->GetNumMaterials();++Slot)
            if (UMaterialInterface* Material=Profile->BodyMaterials[Slot].Get()) Body->CreateDynamicMaterialInstance(Slot,Material);
    if (auto* Anim=Cast<UVamShapeAnimInstance>(Body->GetAnimInstance())) Anim->SetRigProfile(RigProfile.Get());
    if (auto* Motion=GetOwner()->FindComponentByClass<UVamMotionComponent>()) Body->AddTickPrerequisiteComponent(Motion);
    if (auto* Active=GetOwner()->FindComponentByClass<UVamActivePoseComponent>()) Body->AddTickPrerequisiteComponent(Active);
    Body->SetUpdateAnimationInEditor(true);
    FString Missing;
    for (int32 PartIndex=0;PartIndex<LoadedDefinition->Parts.Num();++PartIndex)
    {
        const auto& Reference=LoadedDefinition->Parts[PartIndex];
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
        if (const UVamMaterialProfile* Profile=MaterialProfile.Get())
            if (Profile->PartMaterials.IsValidIndex(PartIndex))
                for (int32 Slot=0;Slot<Profile->PartMaterials[PartIndex].Materials.Num() && Slot<Part->GetNumMaterials();++Slot)
                    if (UMaterialInterface* Material=Profile->PartMaterials[PartIndex].Materials[Slot].Get())
                        Part->CreateDynamicMaterialInstance(Slot,Material);
        LoadedParts.Add(Part);
    }
    ApplyAppearanceState();
    ResetToImportedAppearance();
    if (const UVamAppearancePreset* Preset=AppearancePreset.Get()) PreviewParameters(Preset->Parameters);
    if (!InitialShapeValues.IsEmpty()) PreviewParameters(InitialShapeValues);
    CommitShape();
    UE_LOG(LogTemp, Display, TEXT("VAM_NATIVE_RUNTIME_LOADED Body=%s Parts=%d Morphs=%d PhysicsBodies=%d Constraints=%d Missing=%s"),
        *Mesh->GetPathName(), LoadedParts.Num(), LoadedDefinition->Parameters.Num(),
        PhysicsAsset.Get() ? PhysicsAsset.Get()->SkeletalBodySetups.Num() : 0,
        PhysicsAsset.Get() ? PhysicsAsset.Get()->ConstraintSetup.Num() : 0, *Missing);
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
    if (Body && GetWorld()) State.AnimationPoseTimeSeconds=GetWorld()->GetTimeSeconds();
    if (const auto* Motion=GetOwner()->FindComponentByClass<UVamMotionComponent>())
    {
        State.SimulationShapeRevision=Motion->GetClock().ShapeRevision;
        if (const auto* Interaction=GetOwner()->FindComponentByClass<UVamInteractionComponent>())
            if (Interaction->Mode!=EVamPhysicalMode::Controlled)
            {
                State.bHasSimulation=true;
                State.RigidPoseTimeSeconds=Motion->GetClock().TimeSeconds;
                State.CollisionProxyTimeSeconds=State.RigidPoseTimeSeconds;
            }
    }
    return State; // SurfaceTime stays unavailable until a real surface solver is attached.
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
        TMap<int32,FTransform> DebugOffsets;
        if (const auto* Previous=Cast<UVamShapeAnimInstance>(Body->GetAnimInstance())) DebugOffsets=Previous->GetDebugBoneOffsets();
        // UE computes component-private inverse bind matrices as well as the reference pose.
        // This does not mutate the shared mesh, skeleton, or another character instance.
        Body->SetRefPoseOverride(ShapeReferencePose);
        for (auto Part : LoadedParts) Part->SetRefPoseOverride(ShapeReferencePose);
        Body->InitAnim(true);
        if (auto* Animation=Cast<UVamShapeAnimInstance>(Body->GetAnimInstance()))
        {
            Animation->SetRigProfile(RigProfile.Get());
            for (const auto& Pair:DebugOffsets) Animation->SetDebugBoneOffset(Pair.Key,Pair.Value);
        }
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
    if (bCommitted) if (auto* Motion=GetOwner()->FindComponentByClass<UVamMotionComponent>()) Motion->CommitShapeRevision(Event.ShapeRevision);
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

bool UVamCharacterComponent::SetDebugBoneOffset(int32 BoneIndex, const FTransform& Offset)
{
    if (!Body || !Body->GetSkeletalMeshAsset() || !Body->GetSkeletalMeshAsset()->GetRefSkeleton().IsValidIndex(BoneIndex) ||
        !Offset.IsValid()) return false;
    auto* Animation=Cast<UVamShapeAnimInstance>(Body->GetAnimInstance());
    if (!Animation) return false;
    Animation->SetDebugBoneOffset(BoneIndex,Offset);
    Body->TickAnimation(0.f,false);
    Body->RefreshBoneTransforms();
    return true;
}

FTransform UVamCharacterComponent::GetDebugBoneOffset(int32 BoneIndex) const
{
    if (Body)
        if (const auto* Animation=Cast<UVamShapeAnimInstance>(Body->GetAnimInstance()))
            return Animation->GetDebugBoneOffset(BoneIndex);
    return FTransform::Identity;
}

void UVamCharacterComponent::ResetDebugBoneOffsets()
{
    if (!Body) return;
    if (auto* Animation=Cast<UVamShapeAnimInstance>(Body->GetAnimInstance()))
    {
        Animation->ClearDebugBoneOffsets();
        Body->TickAnimation(0.f,false);
        Body->RefreshBoneTransforms();
    }
}
bool UVamCharacterComponent::SetIKGoal(FName Semantic, const FTransform& WorldGoal)
{
    const UVamRigProfile* Profile=RigProfile.Get();
    auto* Anim=Body ? Cast<UVamShapeAnimInstance>(Body->GetAnimInstance()) : nullptr;
    if (!Profile || !Anim || !Profile->Effectors.Contains(Semantic) || Profile->BoneForSemantic(Semantic).IsNone() || !WorldGoal.IsValid()) return false;
    Anim->SetIKGoal(Semantic,WorldGoal);
    Body->TickAnimation(0.f,false);
    Body->RefreshBoneTransforms();
    return true;
}
void UVamCharacterComponent::ClearIKGoal(FName Semantic)
{
    if (Body) if (auto* Anim=Cast<UVamShapeAnimInstance>(Body->GetAnimInstance()))
    {
        Anim->ClearIKGoal(Semantic);
        Body->TickAnimation(0.f,false);
        Body->RefreshBoneTransforms();
    }
}
bool UVamCharacterComponent::SetFootLocked(FName FootSemantic, bool bLocked)
{
    if (FootSemantic!=TEXT("left_foot") && FootSemantic!=TEXT("right_foot")) return false;
    if (!bLocked) { ClearIKGoal(FootSemantic); return true; }
    const UVamRigProfile* Profile=RigProfile.Get();
    const FName Bone=Profile ? Profile->BoneForSemantic(FootSemantic) : NAME_None;
    if (!Body || Bone.IsNone() || Body->GetBoneIndex(Bone)==INDEX_NONE) return false;
    return SetIKGoal(FootSemantic,Body->GetBoneTransform(Body->GetBoneIndex(Bone)));
}
void UVamCharacterComponent::ApplyAppearanceState()
{
    TArray<USkeletalMeshComponent*> Meshes;
    if (Body) Meshes.Add(Body);
    for (auto Part:LoadedParts) if (Part) Meshes.Add(Part.Get());
    for (USkeletalMeshComponent* Mesh:Meshes)
        for (int32 Slot=0;Slot<Mesh->GetNumMaterials();++Slot)
            if (auto* Instance=Cast<UMaterialInstanceDynamic>(Mesh->GetMaterial(Slot)))
            {
                for (const auto& Pair:AppearanceScalars) Instance->SetScalarParameterValue(Pair.Key,Pair.Value);
                for (const auto& Pair:AppearanceColors) Instance->SetVectorParameterValue(Pair.Key,Pair.Value);
            }
}
bool UVamCharacterComponent::SetAppearanceScalar(FName Parameter, float Value)
{
    if (Parameter.IsNone() || !FMath::IsFinite(Value)) return false;
    AppearanceScalars.Add(Parameter,Value);
    ApplyAppearanceState();
    return true;
}
bool UVamCharacterComponent::SetAppearanceColor(FName Parameter, FLinearColor Value)
{
    if (Parameter.IsNone() || !FMath::IsFinite(Value.R) || !FMath::IsFinite(Value.G) ||
        !FMath::IsFinite(Value.B) || !FMath::IsFinite(Value.A)) return false;
    AppearanceColors.Add(Parameter,Value);
    ApplyAppearanceState();
    return true;
}


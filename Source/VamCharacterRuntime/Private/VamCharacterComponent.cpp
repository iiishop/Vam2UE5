#include "VamCharacterComponent.h"
#include "VamCharacterDefinition.h"
#include "VamRuntimeConfiguration.h"
#include "VamShapeAnimInstance.h"
#include "VamRigProfile.h"
#include "VamMotionComponent.h"
#include "VamActivePoseComponent.h"
#include "VamMaterialProfile.h"
#include "VamInteractionComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/Skeleton.h"
#include "Animation/AnimSequence.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/SkeletalBodySetup.h"
#include "PhysicsEngine/PhysicsConstraintTemplate.h"
#include "VamPhysicsShapeProfile.h"
#include "VamPhysicsOutputComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/World.h"

UVamCharacterComponent::UVamCharacterComponent()
{
    PrimaryComponentTick.bCanEverTick=true;
    PrimaryComponentTick.TickGroup=TG_PrePhysics;
}

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
    InstancePhysics=nullptr; CollisionShapeRevision=INDEX_NONE; LastShapeError.Reset();
    LoadedDefinition = nullptr;
    PreviewState = FVamShapeState(); CommittedState = FVamShapeState(); ShapeReferencePose.Reset();
    PoseControlRotations.Reset();
    ExpressionWeights.Reset();
    FootContacts.Reset();
}

void UVamCharacterComponent::LoadCharacter()
{
    UnloadCharacter();
    if (!RuntimeConfiguration.IsNull())
    {
        const UVamRuntimeConfiguration* Config=RuntimeConfiguration.LoadSynchronous();
        if (!Config || Config->SchemaVersion!=2 || !Config->bIndependentReloadVerified || Config->BuildIdentity.IsEmpty())
        { OnLoaded.Broadcast(false,TEXT("Runtime configuration unavailable or incompatible")); return; }
        Definition=Config->Definition; RigProfile=Config->Rig; PhysicsAsset=Config->Physics;
        PhysicsShapeProfile=Config->PhysicsShape;
        AnimationClass=Config->AnimationClass; MaterialProfile=Config->Materials; BaseAnimation=Config->BaseAnimation;
    }
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
    if (!BaseAnimation.IsNull()) Paths.AddUnique(BaseAnimation.ToSoftObjectPath());
    if (!PhysicsAsset.IsNull()) Paths.AddUnique(PhysicsAsset.ToSoftObjectPath());
    if (!PhysicsShapeProfile.IsNull()) Paths.AddUnique(PhysicsShapeProfile.ToSoftObjectPath());
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
    if (!BaseAnimation.IsNull() && !BaseAnimation.Get())
    { OnLoaded.Broadcast(false,TEXT("Configured base animation did not load")); return; }
    if (const UVamRuntimeConfiguration* Config=RuntimeConfiguration.Get())
    {
        const UVamShapeDefinition* Shape=LoadedDefinition->Shape.Get();
        if (Config->SourceDigest!=LoadedDefinition->SourceDigest || Config->BindSignature!=LoadedDefinition->BindSignature ||
            !Shape || Config->MorphSetLockDigest!=Shape->MorphSetLockDigest)
        { OnLoaded.Broadcast(false,TEXT("Runtime configuration source/bind/MorphSet identity mismatch")); return; }
    }
    if (const UVamRigProfile* Rig=RigProfile.Get())
        if (Rig->Skeleton.Get()!=Mesh->GetSkeleton())
        { OnLoaded.Broadcast(false,TEXT("Stage06 rig profile skeleton does not match body")); return; }
    if (const UPhysicsAsset* Asset=PhysicsAsset.Get())
        if (Asset->SkeletalBodySetups.IsEmpty() || Asset->ConstraintSetup.IsEmpty())
        { OnLoaded.Broadcast(false,TEXT("Stage06 physics asset has no bodies or constraints")); return; }
    Body = NewObject<USkeletalMeshComponent>(GetOwner(), NAME_None, RF_Transient);
    Body->SetupAttachment(this);
    Body->SetSkeletalMeshAsset(Mesh);
    if (UPhysicsAsset* Asset=PhysicsAsset.Get())
    {
        if(!PhysicsShapeProfile.IsNull())
        {
            const auto* Profile=PhysicsShapeProfile.Get();
            if(!Profile || Profile->Physics.Get()!=Asset || Profile->BindSignature!=LoadedDefinition->BindSignature ||
                !LoadedDefinition->Shape.Get() || Profile->MorphSetLockDigest!=LoadedDefinition->Shape.Get()->MorphSetLockDigest)
            { Body=nullptr; OnLoaded.Broadcast(false,TEXT("Collision profile source/bind/MorphSet mismatch")); return; }
            InstancePhysics=DuplicateObject<UPhysicsAsset>(Asset,this);
            InstancePhysics->SetFlags(RF_Transient);
            // Duplication must own every mutable primitive and constraint template.
            for(int32 I=0;I<Asset->SkeletalBodySetups.Num();++I)
                check(InstancePhysics->SkeletalBodySetups[I]!=Asset->SkeletalBodySetups[I]);
            for(int32 I=0;I<Asset->ConstraintSetup.Num();++I)
                check(InstancePhysics->ConstraintSetup[I]!=Asset->ConstraintSetup[I]);
            Body->SetPhysicsAsset(InstancePhysics);
        }
        else Body->SetPhysicsAsset(Asset); // Legacy host retains its traced Stage06 behavior.
    }
    Body->SetAnimInstanceClass(AnimationClass.Get() ? AnimationClass.Get() : UVamShapeAnimInstance::StaticClass());
    Body->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
    Body->SetCollisionEnabled(PhysicsAsset.Get() ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
    if (PhysicsAsset.Get()) Body->SetCollisionResponseToAllChannels(ECR_Block);
    Body->RegisterComponent();
    Body->AddTickPrerequisiteComponent(this);
    if (const UVamMaterialProfile* Profile=MaterialProfile.Get())
        for (int32 Slot=0;Slot<Profile->BodyMaterials.Num() && Slot<Body->GetNumMaterials();++Slot)
            if (UMaterialInterface* Material=Profile->BodyMaterials[Slot].Get()) Body->CreateDynamicMaterialInstance(Slot,Material);
    if (auto* Anim=Cast<UVamShapeAnimInstance>(Body->GetAnimInstance()))
    {
        Anim->SetRigProfile(RigProfile.Get());
        if (!Anim->SetBaseAnimation(BaseAnimation.Get()))
        { Body->DestroyComponent(); Body=nullptr; OnLoaded.Broadcast(false,TEXT("Base animation requires the exact skeleton and a non-additive in-place clip")); return; }
    }
    if (auto* Motion=GetOwner()->FindComponentByClass<UVamMotionComponent>())
    { Body->AddTickPrerequisiteComponent(Motion);AddTickPrerequisiteComponent(Motion); }
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
    if(!CommitShape())
    { const FString Error=LastShapeError;UnloadCharacter();OnLoaded.Broadcast(false,TEXT("Initial shape/collision transaction rejected: ")+Error);return; }
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
    if(Body) if(const auto* Anim=Cast<UVamShapeAnimInstance>(Body->GetAnimInstance()))
    {
        State.AnimationPoseTimeSeconds=Anim->ProducedPoseWorldTimeSeconds;
        State.AnimationPoseRevision=Anim->ProducedPoseRevision;
    }
    if(const auto* Producer=GetOwner()->FindComponentByClass<UVamPhysicsOutputComponent>())
    {
        const auto Output=Producer->GetCollisionOutput();State.bCollisionOutputValid=Output.bValid;
        if(Output.bValid)
        {
            State.PoseComponentSpace=Producer->GetFinalPose();
            State.bPoseFinalized=true;State.FinalPoseWorldTimeSeconds=Output.PublishedWorldTimeSeconds;
            State.FinalPoseAnimationRevision=Output.FinalPoseAnimationRevision;
            State.RigidPoseTimeSeconds=Output.SolverResultsTimeSeconds;
            State.RigidCompletedTimeSeconds=Output.SolverCompletedTimeSeconds;
            State.RigidInputAnimationRevision=Output.AnimationPoseRevision;
            State.CollisionProxyTimeSeconds=Output.SolverResultsTimeSeconds;
            State.SimulationShapeRevision=Output.ShapeRevision;
        }
    }
    State.bHasSimulation=Body && Body->IsAnySimulatingPhysics();
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
    const FVamShapeState Previous=PreviewState;
    for (const auto& Pair : Values)
    {
        const auto& P=*LoadedDefinition->Parameters.FindByPredicate([&Pair](const FVamMorphParameter& X){return X.Target==Pair.Key;});
        const float Value=FMath::Clamp(Pair.Value,P.Minimum,P.Maximum);
        const float* Old=PreviewState.Values.Find(Pair.Key);
        if (!Old || *Old!=Value) { PreviewState.Values.Add(Pair.Key,Value); Changed.Add(Pair.Key); }
    }
    if (!Changed.IsEmpty() && !ApplyShape(Changed,false)) { PreviewState=Previous; return false; }
    return true;
}

bool UVamCharacterComponent::ApplyShape(const TArray<FName>& Changed, bool bCommitted)
{
    if (!Body || !LoadedDefinition) return false;
    // Preserve InitAnim's necessary synchronization without its state reset.
    // A UI/Blueprint transaction may arrive while the previous pose is evaluating.
    Body->HandleExistingParallelEvaluationTask(true,true);
    for (auto Part:LoadedParts) Part->HandleExistingParallelEvaluationTask(true,true);
    auto* Shape=LoadedDefinition->Shape.Get();
    const auto& Ref=Body->GetSkeletalMeshAsset()->GetRefSkeleton();
    const TArray<FTransform> PreviousReference=ShapeReferencePose.IsEmpty() ? Ref.GetRefBonePose() : ShapeReferencePose;
    TArray<FTransform> NextReference=Shape ? Shape->NeutralLocalBind : TArray<FTransform>();
    FVamShapeChange Event;
    Event.Parameters=Changed; Event.bCommitted=bCommitted;
    for (const auto& P : LoadedDefinition->Parameters)
    {
        const float* Found=PreviewState.Values.Find(P.Target);
        const float Absolute=Found ? *Found : P.DefaultValue;
        for (const auto& Offset : P.BoneCenters)
            if (NextReference.IsValidIndex(Offset.BoneIndex))
                NextReference[Offset.BoneIndex].AddToTranslation(Offset.LocalTranslation*Absolute);
        if (Changed.Contains(P.Target))
        {
            for (FName Region : P.AffectedRegions) Event.Regions.AddUnique(Region);
            for (const auto& Offset : P.BoneCenters)
                if (Body->GetSkeletalMeshAsset()->GetRefSkeleton().IsValidIndex(Offset.BoneIndex))
                    Event.Bones.AddUnique(Body->GetSkeletalMeshAsset()->GetRefSkeleton().GetBoneName(Offset.BoneIndex));
        }
    }
    TArray<FTransform> OldCS=PreviousReference, NewCS=NextReference;
    for(int32 I=0;I<NewCS.Num();++I) if(Ref.GetParentIndex(I)>=0)
    { NewCS[I]=NewCS[I]*NewCS[Ref.GetParentIndex(I)]; OldCS[I]=OldCS[I]*OldCS[Ref.GetParentIndex(I)]; }
    TArray<FVamCollisionFit> Fits;
    bool Rebind=false;
    if(const UVamPhysicsShapeProfile* Profile=PhysicsShapeProfile.Get())
    {
        if(!InstancePhysics || !Profile->Fit(PreviewState.Values,NewCS,Fits,LastShapeError)) return false;
        Rebind=CollisionShapeRevision==INDEX_NONE;
        for(const auto& Fit:Fits)
        {
            const int32 Index=InstancePhysics->FindBodyIndex(Fit.Bone);
            if(Index<0 || InstancePhysics->SkeletalBodySetups[Index]->AggGeom.SphylElems.Num()!=1)
            { LastShapeError=TEXT("Instance collision layout mismatch"); return false; }
            const auto& Capsule=InstancePhysics->SkeletalBodySetups[Index]->AggGeom.SphylElems[0];
            Rebind|=!Capsule.Center.Equals(Fit.Center,1.e-5) || !FMath::IsNearlyEqual(Capsule.Radius,Fit.Radius,1.e-5f) ||
                !FMath::IsNearlyEqual(Capsule.Length,Fit.Length,1.e-5f);
        }
        for(int32 I=0;I<NewCS.Num();++I) Rebind|=!NewCS[I].Equals(OldCS[I],1.e-5);
    }
    struct FSavedBody
    {
        FName Bone; int32 Index; FTransform World;
        FVector Linear,Angular; bool Simulating,Awake; float Blend;
    };
    TArray<FSavedBody> SavedBodies;
    auto* Interaction=GetOwner()->FindComponentByClass<UVamInteractionComponent>();
    if(Rebind)
    {
        for(const auto& Setup:InstancePhysics->SkeletalBodySetups)
            if(const FBodyInstance* BI=Body->GetBodyInstance(Setup->BoneName))
                SavedBodies.Add({Setup->BoneName,Ref.FindBoneIndex(Setup->BoneName),BI->GetUnrealWorldTransform(),
                    BI->GetUnrealWorldVelocity(),BI->GetUnrealWorldAngularVelocityInRadians(),BI->IsInstanceSimulatingPhysics(),BI->IsInstanceAwake(),BI->PhysicsBlendWeight});
        SavedBodies.Sort([](const FSavedBody& A,const FSavedBody& B){return A.Index<B.Index;});
        if(Interaction) Interaction->SuspendForShapeRebind();
        Body->DestroyPhysicsState();
        for(const auto& Fit:Fits)
        {
            auto* Setup=InstancePhysics->SkeletalBodySetups[InstancePhysics->FindBodyIndex(Fit.Bone)].Get();
            auto& Capsule=Setup->AggGeom.SphylElems[0];
            Capsule.Center=Fit.Center; Capsule.Radius=Fit.Radius; Capsule.Length=Fit.Length;
            if(Fit.Points.IsEmpty()) Setup->DefaultInstance.SetCollisionEnabled(ECollisionEnabled::NoCollision);
            Setup->InvalidatePhysicsData();
        }
        for(auto& Template:InstancePhysics->ConstraintSetup)
        {
            auto& C=Template->DefaultInstance;
            const FTransform Relative=NewCS[Ref.FindBoneIndex(C.GetChildBoneName())].GetRelativeTransform(NewCS[Ref.FindBoneIndex(C.GetParentBoneName())]);
            C.SetRefFrame(EConstraintFrame::Frame1,FTransform(Relative.GetRotation().Inverse()));
            C.SetRefFrame(EConstraintFrame::Frame2,FTransform(FQuat::Identity,Relative.GetTranslation()));
        }
    }
    ShapeReferencePose=MoveTemp(NextReference);
    ApplyMorphWeights();
    if (ShapeReferencePose.Num()==Body->GetSkeletalMeshAsset()->GetRefSkeleton().GetRawBoneNum())
    {
        // SetRefPoseOverride invalidates RequiredBones in UE; RefreshBoneTransforms
        // rebuilds that cache without restarting the current animation player.
        // Keep the same AnimInstance, IK goals, active offsets and physics handles.
        Body->SetRefPoseOverride(ShapeReferencePose);
        for (auto Part : LoadedParts) Part->SetRefPoseOverride(ShapeReferencePose);
        Body->TickAnimation(0.f,false);
        Body->RefreshBoneTransforms();
    }
    if(Rebind)
    {
        Body->CreatePhysicsState();
        TMap<int32,FTransform> Restored;
        for(const auto& Saved:SavedBodies)
        {
            FBodyInstance* BI=Body->GetBodyInstance(Saved.Bone); if(!BI) continue;
            FTransform World=Body->GetBoneTransform(Saved.Index);
            if(Saved.Simulating)
            {
                int32 Parent=Ref.GetParentIndex(Saved.Index);
                while(Parent>=0 && !Restored.Contains(Parent)) Parent=Ref.GetParentIndex(Parent);
                const FSavedBody* OldParent=SavedBodies.FindByPredicate([Parent](const FSavedBody& B){return B.Index==Parent;});
                if(OldParent)
                {
                    FTransform Relative=Saved.World.GetRelativeTransform(OldParent->World);
                    Relative.AddToTranslation(NewCS[Saved.Index].GetRelativeTransform(NewCS[Parent]).GetTranslation()-OldCS[Saved.Index].GetRelativeTransform(OldCS[Parent]).GetTranslation());
                    World=Relative*Restored[Parent];
                }
                else
                {
                    World=Saved.World;
                    World.AddToTranslation(Body->GetComponentTransform().TransformVector(NewCS[Saved.Index].GetTranslation()-OldCS[Saved.Index].GetTranslation()));
                }
            }
            BI->SetInstanceSimulatePhysics(Saved.Simulating,true,true); BI->PhysicsBlendWeight=Saved.Blend;
            BI->SetBodyTransform(World,ETeleportType::TeleportPhysics,false);
            BI->SetLinearVelocity(Saved.Linear,false,false); BI->SetAngularVelocityInRadians(Saved.Angular,false,false);
            if(Saved.Simulating && !Saved.Awake) BI->PutInstanceToSleep();
            Restored.Add(Saved.Index,World);
        }
        if(Interaction) Interaction->ResumeAfterShapeRebind();
    }
    // A batch can change/reset/restore several times within one game frame.
    // RecreateRenderState refreshes UE's private Morph curve cache at frame end.
    // Multiple edits coalesce to one update; final curves/binds reach every follower.
    Body->MarkRenderStateDirty();
    for (auto Part:LoadedParts) Part->MarkRenderStateDirty();
    for (const auto& Part : LoadedDefinition->Parts) Event.Parts.Add(Part.ToSoftObjectPath());
    Event.ShapeRevision=++PreviewState.Revision;
    if(InstancePhysics) CollisionShapeRevision=Event.ShapeRevision;
    LastShapeError.Reset();
    if (bCommitted) if (auto* Motion=GetOwner()->FindComponentByClass<UVamMotionComponent>()) Motion->CommitShapeRevision(Event.ShapeRevision);
    if (bCommitted) CommittedState=PreviewState;
    OnShapeChanged.Broadcast(Event);
    return true;
}

bool UVamCharacterComponent::SetExpressionWeights(const TMap<FName,float>& Values)
{
    if (!Body || !LoadedDefinition) return false;
    TMap<FName,float> Next;
    for (const auto& Pair:Values)
    {
        const FVamMorphParameter* P=LoadedDefinition->Parameters.FindByPredicate(
            [&Pair](const FVamMorphParameter& Item){return Item.Target==Pair.Key;});
        // Bone-driven expressions need a separate pose layer, not a vertex-only override.
        if (!P || P->Group!=TEXT("Expression") || !P->BoneCenters.IsEmpty() || !FMath::IsFinite(Pair.Value)) return false;
        Next.Add(Pair.Key,FMath::Clamp(Pair.Value,P->Minimum,P->Maximum));
    }
    ExpressionWeights=MoveTemp(Next);
    ApplyMorphWeights();
    return true;
}

void UVamCharacterComponent::ApplyMorphWeights()
{
    if (!Body || !LoadedDefinition) return;
    for (const FVamMorphParameter& P:LoadedDefinition->Parameters)
    {
        const float* Authored=PreviewState.Values.Find(P.Target);
        float Absolute=Authored ? *Authored : P.DefaultValue;
        if (const float* Active=ExpressionWeights.Find(P.Target)) Absolute=FMath::Max(Absolute,*Active);
        const float Weight=Absolute-(LoadedDefinition->ShapeConvention==TEXT("appearance_plus_parameter_offsets") ? P.DefaultValue : 0.f);
        if (Body->GetSkeletalMeshAsset()->FindMorphTarget(P.Target)) Body->SetMorphTarget(P.Target,Weight);
        for (auto Part:LoadedParts)
            if (Part->GetSkeletalMeshAsset()->FindMorphTarget(P.Target)) Part->SetMorphTarget(P.Target,Weight);
    }
}

bool UVamCharacterComponent::CommitShape()
{
    if (!Body || !LoadedDefinition) return false;
    TArray<FName> Changed;
    for (const auto& Pair : PreviewState.Values)
        if (!CommittedState.Values.Contains(Pair.Key) || CommittedState.Values[Pair.Key]!=Pair.Value) Changed.Add(Pair.Key);
    return ApplyShape(Changed,true);
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
bool UVamCharacterComponent::IsPoseControlBone(int32 BoneIndex) const
{
    const UVamRigProfile* Profile=RigProfile.Get();
    if (!Profile || !Body || !Body->GetSkeletalMeshAsset() || BoneIndex<=0 ||
        !Body->GetSkeletalMeshAsset()->GetRefSkeleton().IsValidIndex(BoneIndex)) return false;
    const FName Bone=Body->GetBoneName(BoneIndex);
    const FVamRigJoint* Joint=Profile->Joints.FindByPredicate([Bone](const FVamRigJoint& J){return J.Bone==Bone;});
    return Joint && Joint->Semantic!=Profile->SolverRootSemantic && VamPoseControl::IsEligible(*Joint);
}

bool UVamCharacterComponent::SetPoseControlRotation(int32 BoneIndex, FRotator LocalRotation)
{
    if (!FMath::IsFinite(LocalRotation.Pitch) || !FMath::IsFinite(LocalRotation.Yaw) ||
        !FMath::IsFinite(LocalRotation.Roll) || !IsPoseControlBone(BoneIndex)) return false;
    auto* Animation=Cast<UVamShapeAnimInstance>(Body->GetAnimInstance());
    if (!Animation) return false;
    const FName Bone=Body->GetBoneName(BoneIndex);
    const FVamRigJoint* Joint=RigProfile.Get()->Joints.FindByPredicate([Bone](const FVamRigJoint& J){return J.Bone==Bone;});
    if (!Joint) return false;
    const FRotator Clamped=VamPoseControl::Clamp(*Joint,LocalRotation);
    if (Clamped.IsNearlyZero()) PoseControlRotations.Remove(BoneIndex);
    else PoseControlRotations.Add(BoneIndex,Clamped);
    Animation->SetPoseControlRotation(BoneIndex,Clamped);
    Body->TickAnimation(0.f,false);
    Body->RefreshBoneTransforms();
    return true;
}

FRotator UVamCharacterComponent::GetPoseControlRotation(int32 BoneIndex) const
{
    if (const FRotator* Rotation=PoseControlRotations.Find(BoneIndex)) return *Rotation;
    return FRotator::ZeroRotator;
}

void UVamCharacterComponent::ResetPoseControlRotations()
{
    PoseControlRotations.Reset();
    if (!Body) return;
    if (auto* Animation=Cast<UVamShapeAnimInstance>(Body->GetAnimInstance()))
    {
        Animation->ClearPoseControlRotations();
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
    if (!bLocked) { FootContacts.Remove(FootSemantic);ClearIKGoal(FootSemantic); return true; }
    const UVamRigProfile* Profile=RigProfile.Get();
    const FName Bone=Profile ? Profile->BoneForSemantic(FootSemantic) : NAME_None;
    if (!Body || Bone.IsNone() || Body->GetBoneIndex(Bone)==INDEX_NONE) return false;
    auto* Anim=Cast<UVamShapeAnimInstance>(Body->GetAnimInstance());if(!Anim) return false;
    FFootContact Contact;Contact.Goal=Body->GetBoneTransform(Body->GetBoneIndex(Bone));Contact.Anchor=Contact.Goal.GetLocation();
    if(!UpdateFootContact(FootSemantic,Contact)) return false;
    FootContacts.Add(FootSemantic,Contact);
    Anim->SetGroundContactGoal(FootSemantic,Contact.Goal);
    Body->TickAnimation(0.f,false);Body->RefreshBoneTransforms();return true;
}
bool UVamCharacterComponent::GetFootContactGoal(FName Semantic,FTransform& Goal) const
{
    const auto* Contact=FootContacts.Find(Semantic);
    if(!Contact || !Contact->Valid) return false;
    Goal=Contact->Goal;return true;
}
bool UVamCharacterComponent::UpdateFootContact(FName Semantic,FFootContact& Contact)
{
    Contact.Valid=false;
    const auto* Rig=RigProfile.Get();
    if(!Body || !Rig || !Body->GetPhysicsAsset() || !GetWorld()) return false;
    const FName Bone=Rig->BoneForSemantic(Semantic);
    const int32 BodyIndex=Body->GetPhysicsAsset()->FindBodyIndex(Bone);
    if(BodyIndex<0) return false;
    const auto* Setup=Body->GetPhysicsAsset()->SkeletalBodySetups[BodyIndex].Get();
    if(Setup->AggGeom.SphylElems.IsEmpty()) return false;
    if(Contact.Support.IsValid()) Contact.Anchor=Contact.Support->GetComponentTransform().TransformPosition(Contact.SupportLocalPoint);
    FHitResult Hit;FCollisionQueryParams Params(SCENE_QUERY_STAT(VamFootGround),false,GetOwner());
    if(!GetWorld()->LineTraceSingleByChannel(Hit,Contact.Anchor+FVector(0,0,FootProbeAboveCm),Contact.Anchor-FVector(0,0,FootProbeBelowCm),ECC_Visibility,Params) ||
        !Hit.bBlockingHit || Hit.ImpactNormal.Z<FMath::Cos(FMath::DegreesToRadians(MaximumGroundSlopeDegrees))) return false;
    const FVector Normal=Hit.ImpactNormal.GetSafeNormal();
    FTransform Goal=Contact.Goal;
    Goal.SetRotation((FQuat::FindBetweenNormals(Contact.Normal,Normal)*Goal.GetRotation()).GetNormalized());
    double Lowest=TNumericLimits<double>::Max();
    for(const auto& Capsule:Setup->AggGeom.SphylElems)
    {
        const FVector Axis=Goal.TransformVector(Capsule.Rotation.Quaternion().GetAxisZ()).GetSafeNormal();
        const FVector Scale=Goal.GetScale3D().GetAbs();
        const double Extent=Capsule.Radius*FMath::Max(Scale.X,Scale.Y)+Capsule.Length*.5*Scale.Z*FMath::Abs(FVector::DotProduct(Axis,Normal));
        Lowest=FMath::Min(Lowest,FVector::DotProduct(Goal.TransformVector(Capsule.Center),Normal)-Extent);
    }
    Goal.SetLocation(Hit.ImpactPoint-Normal*Lowest);
    Contact.Goal=Goal;Contact.Normal=Normal;Contact.Support=Hit.GetComponent();
    if(Contact.Support.IsValid()) Contact.SupportLocalPoint=Contact.Support->GetComponentTransform().InverseTransformPosition(Hit.ImpactPoint);
    Contact.Valid=true;return true;
}
void UVamCharacterComponent::TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime,TickType,ThisTickFunction);
    auto* Anim=Body ? Cast<UVamShapeAnimInstance>(Body->GetAnimInstance()) : nullptr;
    if(!Anim) return;
    if(const auto* Motion=GetOwner()->FindComponentByClass<UVamMotionComponent>())
        if(Motion->GetClock().TeleportRevision!=FootTeleportRevision)
        {
            FootTeleportRevision=Motion->GetClock().TeleportRevision;
            for(const auto& Pair:FootContacts) Anim->ClearIKGoal(Pair.Key);
            FootContacts.Reset(); // Explicit teleport invalidates world contact anchors.
        }
    for(auto& Pair:FootContacts)
    {
        if(UpdateFootContact(Pair.Key,Pair.Value)) Anim->SetGroundContactGoal(Pair.Key,Pair.Value.Goal);
        else Anim->ClearIKGoal(Pair.Key);
    }
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


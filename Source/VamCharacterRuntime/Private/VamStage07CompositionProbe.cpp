#include "VamStage07CompositionProbe.h"
#include "VamCharacterActor.h"
#include "VamCharacterComponent.h"
#include "VamCharacterDefinition.h"
#include "VamShapeAnimInstance.h"
#include "VamRigProfile.h"
#include "VamActivePoseComponent.h"
#include "VamInteractionComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/SkeletalBodySetup.h"
#include "PhysicsEngine/PhysicsConstraintTemplate.h"
#include "VamPhysicsOutputComponent.h"
#include "Animation/AnimSequence.h"
#include "VamMotionComponent.h"
#include "Kismet/GameplayStatics.h"
#include "UnrealClient.h"

namespace
{
TArray<FVector> CollisionSnapshot(const UPhysicsAsset* Asset)
{
    TArray<FVector> Result;
    for(const auto& Setup:Asset->SkeletalBodySetups) for(const auto& Capsule:Setup->AggGeom.SphylElems)
    { Result.Add(Capsule.Center); Result.Add(FVector(Capsule.Radius,Capsule.Length,0)); }
    for(const auto& Template:Asset->ConstraintSetup)
    { Result.Add(Template->DefaultInstance.GetRefFrame(EConstraintFrame::Frame1).GetTranslation()); Result.Add(Template->DefaultInstance.GetRefFrame(EConstraintFrame::Frame2).GetTranslation()); }
    return Result;
}
}

AVamStage07CompositionProbe::AVamStage07CompositionProbe()
{
    PrimaryActorTick.bCanEverTick=true;
    PrimaryActorTick.bTickEvenWhenPaused=true;
    // EndPhysics has finished before these measurements, including physical blend.
    PrimaryActorTick.TickGroup=TG_PostUpdateWork;
}
void AVamStage07CompositionProbe::BeginPlay()
{
    Super::BeginPlay();
    bEnabled=FParse::Param(FCommandLine::Get(),TEXT("VamStage07Composition"));
    Started=GetWorld()->GetTimeSeconds();
    for(const auto& Subject:Subjects) if(Subject && Subject->PhysicsOutput) AddTickPrerequisiteComponent(Subject->PhysicsOutput);
}
void AVamStage07CompositionProbe::Finish(bool Passed,const FString& Reason)
{
    TSharedRef<FJsonObject> Report=MakeShared<FJsonObject>();
    Report->SetBoolField(TEXT("composition_passed"),Passed);
    Report->SetBoolField(TEXT("stage07_passed"),false);
    Report->SetBoolField(TEXT("timing_motion_settle_passed"),TimingPassed);
    Report->SetStringField(TEXT("reason"),Reason);
    Report->SetNumberField(TEXT("final_ik_tolerance_cm"),FinalIKToleranceCm);
    Report->SetNumberField(TEXT("minimum_base_motion_cm"),MinimumBaseMotionCm);
    Report->SetNumberField(TEXT("minimum_grab_motion_cm"),MinimumGrabMotionCm);
    TArray<TSharedPtr<FJsonValue>> Rows;
    for(int32 I=0;I<Samples.Num();++I)
    {
        const FSample& S=Samples[I]; TSharedRef<FJsonObject> Row=MakeShared<FJsonObject>();
        Row->SetStringField(TEXT("configuration"),Subjects[I]->Character->RuntimeConfiguration.ToString());
        Row->SetNumberField(TEXT("clock_travel_seconds"),S.ClockTravel);
        Row->SetNumberField(TEXT("base_motion_cm"),S.BaseMotion);
        Row->SetNumberField(TEXT("final_ik_max_error_cm"),S.IKError);
        Row->SetNumberField(TEXT("blink_peak"),S.Blink); Row->SetNumberField(TEXT("breath_peak"),S.Breath);
        Row->SetNumberField(TEXT("grab_motion_cm"),S.GrabMotion);
        Row->SetNumberField(TEXT("release_velocity_delta_cm_s"),S.ReleaseVelocityDelta);
        Row->SetNumberField(TEXT("shape_transactions"),S.ShapeTransactions);
        Row->SetNumberField(TEXT("shape_velocity_delta_cm_s"),S.ShapeVelocityDelta);
        Row->SetNumberField(TEXT("final_foot_error_cm"),S.FootError);
        Row->SetNumberField(TEXT("collider_change_cm"),S.ColliderChange);
        Row->SetNumberField(TEXT("maximum_physics_bridge_delay_seconds"),S.MaximumPhysicsDelay);
        Row->SetStringField(TEXT("shape_parameter"),S.ShapeParameter.ToString());
        Row->SetNumberField(TEXT("settled_speed_cm_s"),S.SettledSpeed);
        Row->SetNumberField(TEXT("settled_drift_cm"),S.SettledDrift);
        Row->SetNumberField(TEXT("final_joint_limit_error_degrees"),S.JointLimitError);
        Rows.Add(MakeShared<FJsonValueObject>(Row));
    }
    Report->SetArrayField(TEXT("subjects"),Rows);
    FString Json;FJsonSerializer::Serialize(Report,TJsonWriterFactory<>::Create(&Json));
    FFileHelper::SaveStringToFile(Json,*(FPaths::ProjectSavedDir()/TEXT("Stage07Composition.json")));
    UE_LOG(LogTemp,Display,TEXT("VAM_STAGE07_COMPOSITION %s"),*Json);
    bEnabled=false;FPlatformMisc::RequestExit(false);
}
void AVamStage07CompositionProbe::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if(!bEnabled) return;
    const double Now=GetWorld()->GetTimeSeconds();
    if(Now-Started>70) { Finish(false,TEXT("timeout"));return; }
    if(Subjects.Num()<2) { Finish(false,TEXT("two_subjects_required"));return; }
    for(const auto& Actor:Subjects) if(!Actor || !Actor->Character->Body) return;
    if(Phase>=5) { TickTiming(Now);return; }
    if(Phase==0)
    {
        Samples.SetNum(Subjects.Num());
        for(int32 I=0;I<Subjects.Num();++I)
        {
            AVamCharacterActor* Actor=Subjects[I];UVamCharacterComponent* C=Actor->Character;USkeletalMeshComponent* Body=C->Body;
            auto* Anim=Cast<UVamShapeAnimInstance>(Body->GetAnimInstance());const auto* Rig=C->RigProfile.Get();
            if(!Anim || !Anim->GetBaseAnimation() || !Rig) { Finish(false,TEXT("default_host_base_animation_missing"));return; }
            FSample& S=Samples[I];S.LeftHand=Rig->BoneForSemantic(TEXT("left_hand"));S.RightHand=Rig->BoneForSemantic(TEXT("right_hand"));
            if(Body->GetBoneIndex(S.LeftHand)<0 || Body->GetBoneIndex(S.RightHand)<0) { Finish(false,TEXT("hand_mapping_missing"));return; }
            Actor->ActivePose->bBreathing=false;Actor->ActivePose->bIdle=false;Actor->ActivePose->bBlink=false;
            S.InitialPose=Body->GetComponentSpaceTransforms();S.PreviousClock=Anim->GetBaseAnimationTime();
        }
        Phase=1;PhaseStarted=Now;return;
    }
    for(int32 I=0;I<Subjects.Num();++I)
    {
        AVamCharacterActor* Actor=Subjects[I];UVamCharacterComponent* C=Actor->Character;USkeletalMeshComponent* Body=C->Body;FSample& S=Samples[I];
        auto* Anim=Cast<UVamShapeAnimInstance>(Body->GetAnimInstance());
        if(!Anim) { Finish(false,TEXT("animation_instance_lost"));return; }
        if(Phase>=2 && Now-PhaseStarted>.25)
        {
            const auto Output=Actor->PhysicsOutput->GetCollisionOutput();
            if(!Output.bValid || Output.Capsules.IsEmpty() || Output.AnimationPoseRevision<1)
            { Finish(false,TEXT("chaos_collision_publication_unavailable"));return; }
            S.MaximumPhysicsDelay=FMath::Max(S.MaximumPhysicsDelay,FMath::Abs(Output.SolverResultsTimeSeconds-Output.SolverCompletedTimeSeconds));
            FTransform FootGoal;
            if(!C->GetFootContactGoal(TEXT("left_foot"),FootGoal)) { Finish(false,TEXT("ground_contact_lost"));return; }
            const FName Foot=C->RigProfile.Get()->BoneForSemantic(TEXT("left_foot"));
            S.FootError=FMath::Max(S.FootError,float((Body->GetBoneLocation(Foot)-FootGoal.GetLocation()).Size()));
            if(S.FootError>FinalFootToleranceCm) { Finish(false,TEXT("final_ground_contact_error"));return; }
            const auto Reference=C->GetShapeReferencePose();const auto& Pose=Body->GetComponentSpaceTransforms();
            for(const auto& Joint:C->RigProfile.Get()->Joints)
            {
                if(!VamPoseControl::IsEligible(Joint)) continue;
                const int32 BoneIndex=Body->GetBoneIndex(Joint.Bone);if(!Pose.IsValidIndex(BoneIndex) || !Reference.IsValidIndex(BoneIndex)) continue;
                const int32 Parent=Body->GetBoneIndex(Body->GetParentBone(Joint.Bone));
                const FTransform Local=Parent>=0 ? Pose[BoneIndex].GetRelativeTransform(Pose[Parent]) : Pose[BoneIndex];
                const FQuat Delta=(Local.GetRotation()*Reference[BoneIndex].GetRotation().Inverse()).GetNormalized();
                const FQuat Limited=VamPoseControl::Clamp(Joint,Delta.Rotator()).Quaternion();
                S.JointLimitError=FMath::Max(S.JointLimitError,float(FMath::RadiansToDegrees(Delta.AngularDistance(Limited))));
            }
            if(S.JointLimitError>JointLimitToleranceDegrees) { Finish(false,TEXT("final_physics_blend_joint_limit_error"));return; }
        }
        double Travel=Anim->GetBaseAnimationTime()-S.PreviousClock;
        if(Travel<0 && Anim->GetBaseAnimation()) Travel+=Anim->GetBaseAnimation()->GetPlayLength();
        S.ClockTravel+=Travel;S.PreviousClock=Anim->GetBaseAnimationTime();
        if(Phase==1)
            for(int32 B=0;B<S.InitialPose.Num() && B<Body->GetComponentSpaceTransforms().Num();++B)
                S.BaseMotion=FMath::Max(S.BaseMotion,float((S.InitialPose[B].GetLocation()-Body->GetComponentSpaceTransforms()[B].GetLocation()).Size()));
        if(Phase>=2)
        {
            if(Now-PhaseStarted>.25) S.IKError=FMath::Max(S.IKError,float((Body->GetBoneLocation(S.LeftHand)-S.Goal.GetLocation()).Size()));
            S.Breath=FMath::Max(S.Breath,FMath::Abs(Actor->ActivePose->BreathValue));
            for(FName Target:Actor->ActivePose->BlinkMorphTargets) S.Blink=FMath::Max(S.Blink,Body->GetMorphTarget(Target));
            if(S.IKError>FinalIKToleranceCm) { Finish(false,TEXT("final_post_transaction_ik_error"));return; }
        }
    }
    if(Phase==1 && Now-PhaseStarted>1)
    {
        for(int32 I=0;I<Subjects.Num();++I)
        {
            AVamCharacterActor* Actor=Subjects[I];UVamCharacterComponent* C=Actor->Character;USkeletalMeshComponent* Body=C->Body;FSample& S=Samples[I];const auto* Rig=C->RigProfile.Get();
            if(S.BaseMotion<MinimumBaseMotionCm || S.ClockTravel<.2) { Finish(false,TEXT("base_clip_not_advancing_visible_pose"));return; }
            S.Goal=Body->GetBoneTransform(Body->GetBoneIndex(S.LeftHand));
            const FVector ToShoulder=(Body->GetBoneLocation(Rig->BoneForSemantic(TEXT("left_shoulder")))-S.Goal.GetLocation()).GetSafeNormal();
            S.Goal.AddToTranslation(ToShoulder*4+FVector(0,0,4));
            if(!C->SetIKGoal(TEXT("left_hand"),S.Goal) || !Actor->Interaction->SetPhysicalMode(EVamPhysicalMode::LocalResponse,Rig->BoneForSemantic(TEXT("right_elbow"))))
            { Finish(false,TEXT("ik_or_local_physics_rejected"));return; }
            Actor->ActivePose->bBreathing=true;Actor->ActivePose->bBlink=true;Actor->ActivePose->BlinkIntervalSeconds=.6f;
            if(!C->SetFootLocked(TEXT("left_foot"),true)) { Finish(false,TEXT("ground_contact_query_failed"));return; }
            const int32 Chest=Body->GetBoneIndex(Rig->BoneForSemantic(TEXT("chest")));
            if(!C->SetPoseControlRotation(Chest,FRotator(0,3,0))) { Finish(false,TEXT("pose_layer_rejected"));return; }
            const double Clock=CastChecked<UVamShapeAnimInstance>(Body->GetAnimInstance())->GetBaseAnimationTime();
            C->CommitShape();
            if(FMath::Abs(CastChecked<UVamShapeAnimInstance>(Body->GetAnimInstance())->GetBaseAnimationTime()-Clock)>1.e-6 || !Body->IsAnySimulatingPhysics())
            { Finish(false,TEXT("same_shape_commit_reset_animation_or_physics"));return; }
        }
        Phase=2;PhaseStarted=Now;
    }
    else if(Phase==2 && Now-PhaseStarted>1.2)
    {
        for(int32 I=0;I<Subjects.Num();++I)
        {
            AVamCharacterActor* Actor=Subjects[I];FSample& S=Samples[I];
            if(S.IKError>FinalIKToleranceCm || S.Breath<.02 || S.Blink<.6) { Finish(false,TEXT("final_pose_or_active_layer_error"));return; }
            S.GrabStart=Actor->Character->Body->GetBoneLocation(S.RightHand);
            if(!Actor->Interaction->GrabBone(S.RightHand,S.GrabStart)) { Finish(false,TEXT("grab_rejected"));return; }
            Actor->Interaction->MoveGrab(S.GrabStart+FVector(0,0,8));
        }
        Phase=3;PhaseStarted=Now;
    }
    else if(Phase==3 && Now-PhaseStarted>.8)
    {
        if(FParse::Param(FCommandLine::Get(),TEXT("VamStage07Screenshot")))
            FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Stage07Composition.png"),false,false);
        for(int32 I=0;I<Subjects.Num();++I)
        {
            AVamCharacterActor* Actor=Subjects[I];USkeletalMeshComponent* Body=Actor->Character->Body;FSample& S=Samples[I];
            S.GrabMotion=(Body->GetBoneLocation(S.RightHand)-S.GrabStart).Size();
            if(S.GrabMotion<MinimumGrabMotionCm || S.IKError>FinalIKToleranceCm) { Finish(false,TEXT("grab_or_final_ik_failed"));return; }
            const FVector Before=Body->GetPhysicsLinearVelocity(S.RightHand);Actor->Interaction->ReleaseGrab();
            S.ReleaseVelocityDelta=(Body->GetPhysicsLinearVelocity(S.RightHand)-Before).Size();
            if(S.ReleaseVelocityDelta>1.e-3) { Finish(false,TEXT("release_reset_velocity"));return; }
        }
        Phase=4;PhaseStarted=Now;
    }
    else if(Phase==4 && Now-PhaseStarted>.7)
    {
        if(ShapeCycle>=Subjects.Num()*3)
        {
            for(int32 I=0;I<Subjects.Num();++I)
            {
                auto* A=Subjects[I].Get();auto* C=A->Character.Get();
                A->Interaction->ReleaseGrab();A->ActivePose->bBreathing=A->ActivePose->bBlink=A->ActivePose->bIdle=false;
                CastChecked<UVamShapeAnimInstance>(C->Body->GetAnimInstance())->SetBaseAnimation(nullptr);
                C->ClearIKGoal(TEXT("left_hand"));A->Motion->SetPreviewPaused(true);
                Samples[I].WitnessTime=A->Motion->GetClock().TimeSeconds;Samples[I].PhysicsTime=A->PhysicsOutput->GetCollisionOutput().SolverCompletedTimeSeconds;
            }
            Phase=5;PhaseStarted=Now;return;
        }
        const int32 Subject=ShapeCycle/3, Step=ShapeCycle%3;
        AVamCharacterActor* Actor=Subjects[Subject];UVamCharacterComponent* C=Actor->Character;
        USkeletalMeshComponent* Body=C->Body;FSample& S=Samples[Subject];
        const auto* Definition=C->Definition.Get();
        const auto* Parameter=Definition->Parameters.FindByPredicate([](const FVamMorphParameter& P){return P.Group==TEXT("Shape") && !P.BoneCenters.IsEmpty() && P.Maximum>P.Minimum;});
        if(!Parameter || C->PhysicsShapeProfile.IsNull() || Body->GetPhysicsAsset()==C->PhysicsAsset.Get())
        { Finish(false,TEXT("shape_collision_profile_or_parameter_missing"));return; }
        const float Value=FMath::Lerp(Parameter->Minimum,Parameter->Maximum,Step*.5f);
        S.ShapeParameter=Parameter->Target;
        TArray<TArray<FVector>> Others;
        for(const auto& Other:Subjects) Others.Add(CollisionSnapshot(Other->Character->Body->GetPhysicsAsset()));
        const auto Shared=CollisionSnapshot(C->PhysicsAsset.Get());
        if(!Actor->Interaction->GrabBone(S.RightHand,Body->GetBoneLocation(S.RightHand)))
        { Finish(false,TEXT("shape_regrab_rejected"));return; }
        Actor->Interaction->MoveGrab(Body->GetBoneLocation(S.RightHand)+FVector(0,0,4));
        auto* Anim=CastChecked<UVamShapeAnimInstance>(Body->GetAnimInstance());const double Clock=Anim->GetBaseAnimationTime();
        TMap<FName,FVector> Velocities;
        for(const auto& Setup:Body->GetPhysicsAsset()->SkeletalBodySetups) Velocities.Add(Setup->BoneName,Body->GetPhysicsLinearVelocity(Setup->BoneName));
        if(!C->PreviewParameters({{Parameter->Target,Value}}) || !C->CommitShape())
        { Finish(false,TEXT("shape_transaction_rejected: ")+C->LastShapeError);return; }
        const auto Committed=CollisionSnapshot(Body->GetPhysicsAsset());
        const float Temporary=Step==0 ? Parameter->Maximum : Parameter->Minimum;
        if(!C->PreviewParameters({{Parameter->Target,Temporary}})) { Finish(false,TEXT("temporary_preview_rejected"));return; }
        const auto TemporaryCollision=CollisionSnapshot(Body->GetPhysicsAsset());
        for(int32 I=0;I<Committed.Num();++I) S.ColliderChange=FMath::Max(S.ColliderChange,float((Committed[I]-TemporaryCollision[I]).Size()));
        if(S.ColliderChange<1.e-4) { Finish(false,TEXT("shape_did_not_change_actual_colliders"));return; }
        C->CancelShape();
        if(CollisionSnapshot(Body->GetPhysicsAsset())!=Committed || C->GetShapeState().Values.FindRef(Parameter->Target)!=Value ||
            C->CollisionShapeRevision!=C->GetShapeState().Revision || !Actor->Interaction->IsGrabbing() ||
            Body->GetAnimInstance()!=Anim || FMath::Abs(Anim->GetBaseAnimationTime()-Clock)>1.e-6)
        { Finish(false,TEXT("shape_cancel_or_state_retention_failed"));return; }
        for(const auto& Pair:Velocities) S.ShapeVelocityDelta=FMath::Max(S.ShapeVelocityDelta,float((Body->GetPhysicsLinearVelocity(Pair.Key)-Pair.Value).Size()));
        if(S.ShapeVelocityDelta>1.e-3 || CollisionSnapshot(C->PhysicsAsset.Get())!=Shared)
        { Finish(false,TEXT("shape_velocity_or_shared_asset_mutation"));return; }
        for(int32 I=0;I<Subjects.Num();++I) if(I!=Subject && CollisionSnapshot(Subjects[I]->Character->Body->GetPhysicsAsset())!=Others[I])
        { Finish(false,TEXT("cross_instance_collision_mutation"));return; }
        Actor->Interaction->ReleaseGrab();
        if(!Actor->Interaction->GrabBone(S.RightHand,Body->GetBoneLocation(S.RightHand))) { Finish(false,TEXT("post_shape_regrab_failed"));return; }
        ++S.ShapeTransactions;++ShapeCycle;PhaseStarted=Now;
    }
}

void AVamStage07CompositionProbe::TickTiming(double Now)
{
    if(Phase==5 && Now-PhaseStarted>.4)
    {
        for(int32 I=0;I<Subjects.Num();++I)
        {
            auto* A=Subjects[I].Get();auto& S=Samples[I];
            if(A->Motion->GetClock().TimeSeconds!=S.WitnessTime || A->PhysicsOutput->GetCollisionOutput().SolverCompletedTimeSeconds<=S.PhysicsTime)
            { Finish(false,TEXT("witness_pause_scope_incorrect"));return; }
            A->Motion->StepPreview();
            if(FMath::Abs(A->Motion->GetClock().TimeSeconds-S.WitnessTime-A->Motion->FixedStepSeconds)>1.e-6)
            { Finish(false,TEXT("witness_single_step_incorrect"));return; }
            A->Motion->ResetPreview();if(!A->Motion->GetClock().bPaused) { Finish(false,TEXT("reset_lost_pause_state"));return; }
            A->Motion->SetPreviewPaused(false);
            FTransform Destination=A->GetActorTransform();Destination.AddToTranslation(FVector(40,0,0));
            A->Motion->TeleportTo(Destination,Now);
            FTransform OldGoal;
            if(A->Interaction->IsGrabbing() || A->Character->GetFootContactGoal(TEXT("left_foot"),OldGoal) ||
                A->PhysicsOutput->GetCollisionOutput().bValid || A->Character->Body->GetPhysicsLinearVelocity(S.RightHand).Size()>1.e-3)
            { Finish(false,TEXT("teleport_kept_stale_anchor_velocity_or_output"));return; }
        }
        Phase=6;PhaseStarted=Now;
    }
    else if(Phase==6 && Now-PhaseStarted>.4)
    {
        for(int32 I=0;I<Subjects.Num();++I)
        {
            auto* A=Subjects[I].Get();auto& S=Samples[I];const auto Output=A->PhysicsOutput->GetCollisionOutput();
            if(!Output.bValid || Output.CompletedStepsSinceRebind<4 || Output.TeleportRevision!=A->Motion->GetClock().TeleportRevision ||
                !A->Character->SetFootLocked(TEXT("left_foot"),true))
            { Finish(false,TEXT("teleport_recovery_failed"));return; }
            S.PhysicsTime=Output.SolverCompletedTimeSeconds;S.WitnessTime=A->Motion->GetClock().TimeSeconds;
        }
        if(!UGameplayStatics::SetGamePaused(GetWorld(),true)) { Finish(false,TEXT("world_pause_rejected"));return; }
        PausedWallTime=FPlatformTime::Seconds();Phase=7;
    }
    else if(Phase==7 && FPlatformTime::Seconds()-PausedWallTime>.3)
    {
        for(int32 I=0;I<Subjects.Num();++I)
            if(Subjects[I]->PhysicsOutput->GetCollisionOutput().SolverCompletedTimeSeconds!=Samples[I].PhysicsTime ||
                Subjects[I]->Motion->GetClock().TimeSeconds!=Samples[I].WitnessTime)
            { Finish(false,TEXT("world_pause_did_not_freeze_solvers"));return; }
        UGameplayStatics::SetGamePaused(GetWorld(),false);
        // Deliberate short render/game-thread hitch; the engine owns physics advancement.
        FPlatformProcess::Sleep(.12f);Phase=8;PhaseStarted=Now;
    }
    else if(Phase==8 && Now-PhaseStarted>3)
    {
        for(int32 I=0;I<Subjects.Num();++I)
        {
            auto* A=Subjects[I].Get();auto& S=Samples[I];
            if(!A->PhysicsOutput->GetCollisionOutput().bValid || A->PhysicsOutput->GetCollisionOutput().SolverCompletedTimeSeconds<=S.PhysicsTime)
            { Finish(false,TEXT("pause_hitch_recovery_failed"));return; }
            S.SettledPosition=A->Character->Body->GetBoneLocation(S.RightHand);S.MotionStart=A->GetActorTransform();
        }
        Phase=9;PhaseStarted=Now;
    }
    else if((Phase==9 || Phase==12 || Phase==15) && Now-PhaseStarted>1)
    {
        for(int32 I=0;I<Subjects.Num();++I)
        {
            auto* A=Subjects[I].Get();auto& S=Samples[I];
            S.SettledSpeed=FMath::Max(S.SettledSpeed,float(A->Character->Body->GetPhysicsLinearVelocity(S.RightHand).Size()));
            S.SettledDrift=FMath::Max(S.SettledDrift,float((A->Character->Body->GetBoneLocation(S.RightHand)-S.SettledPosition).Size()));
            if(S.SettledSpeed>SettledSpeedToleranceCmS || S.SettledDrift>SettledDriftToleranceCm)
            { Finish(false,TEXT("stationary_rigid_response_drift"));return; }
            S.MotionStart=A->GetActorTransform();
        }
        if(Phase==15) { TimingPassed=true;Finish(true,TEXT("composition_shape_ground_clock_motion_and_settle"));return; }
        ++Phase;PhaseStarted=Now;
    }
    else if(Phase==10 || Phase==13)
    {
        const double Alpha=FMath::Clamp(Now-PhaseStarted,0.,1.);
        for(int32 I=0;I<Subjects.Num();++I)
        {
            auto* A=Subjects[I].Get();FTransform Target=Samples[I].MotionStart;
            A->Character->SetFootLocked(TEXT("left_foot"),false);
            if(Phase==10) Target.AddToTranslation(FVector(20*Alpha,0,0));
            else Target.SetRotation(FQuat(FVector::UpVector,FMath::DegreesToRadians(15*Alpha))*Target.GetRotation());
            A->Motion->MoveContinuously(Target,Now);
        }
        if(Alpha>=1) { ++Phase;PhaseStarted=Now; }
    }
    else if((Phase==11 || Phase==14) && Now-PhaseStarted>3)
    {
        for(int32 I=0;I<Subjects.Num();++I)
        {
            auto* A=Subjects[I].Get();auto& S=Samples[I];
            if(!A->PhysicsOutput->GetCollisionOutput().bValid) { Finish(false,TEXT("motion_collision_output_invalid"));return; }
            S.SettledPosition=A->Character->Body->GetBoneLocation(S.RightHand);
        }
        ++Phase;PhaseStarted=Now;
    }
}

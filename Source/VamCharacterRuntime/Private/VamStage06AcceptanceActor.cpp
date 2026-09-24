#include "VamStage06AcceptanceActor.h"
#include "VamCharacterActor.h"
#include "VamCharacterComponent.h"
#include "VamInteractionComponent.h"
#include "VamMotionComponent.h"
#include "VamActivePoseComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "Kismet/GameplayStatics.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

AVamStage06AcceptanceActor::AVamStage06AcceptanceActor()
{
    PrimaryActorTick.bCanEverTick=true;
    PrimaryActorTick.TickGroup=TG_PostPhysics;
}
void AVamStage06AcceptanceActor::BeginPlay()
{
    Super::BeginPlay();
    bEnabled=FParse::Param(FCommandLine::Get(),TEXT("VamStage06Acceptance"));
    Started=GetWorld()->GetTimeSeconds();
}
void AVamStage06AcceptanceActor::Finish(bool bPassed, const FString& Reason)
{
    const FString Path=FPaths::ProjectSavedDir()/TEXT("Stage06Acceptance.json");
    const FString Json=FString::Printf(TEXT("{\"status\":\"%s\",\"reason\":\"%s\",\"max_witness_cm\":%.6f,\"max_blink_left\":%.6f,\"max_blink_right\":%.6f,\"physics_bodies\":%d,\"constraints\":%d}"),
        bPassed ? TEXT("passed") : TEXT("failed"),*Reason,MaxWitness,MaxBlinkLeft,MaxBlinkRight,
        Primary && Primary->Character && Primary->Character->Body && Primary->Character->Body->GetPhysicsAsset() ?
            Primary->Character->Body->GetPhysicsAsset()->SkeletalBodySetups.Num() : 0,
        Primary && Primary->Character && Primary->Character->Body && Primary->Character->Body->GetPhysicsAsset() ?
            Primary->Character->Body->GetPhysicsAsset()->ConstraintSetup.Num() : 0);
    FFileHelper::SaveStringToFile(Json,*Path);
    UE_LOG(LogTemp,Display,TEXT("VAM_STAGE06_ACCEPTANCE %s"),*Json);
    bEnabled=false;
    FPlatformMisc::RequestExit(false);
}
void AVamStage06AcceptanceActor::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (!bEnabled) return;
    const double Now=GetWorld()->GetTimeSeconds();
    if (Now-Started>25) { Finish(false,TEXT("timeout")); return; }
    if (!Primary || !Secondary)
    {
        TArray<AActor*> Actors;
        UGameplayStatics::GetAllActorsOfClass(GetWorld(),AVamCharacterActor::StaticClass(),Actors);
        if (Actors.Num()<2) return;
        Primary=Cast<AVamCharacterActor>(Actors[0]); Secondary=Cast<AVamCharacterActor>(Actors[1]);
    }
    if (!Primary->Character->Body || !Secondary->Character->Body) return;
    USkeletalMeshComponent* Body=Primary->Character->Body;
    USkeletalMeshComponent* Other=Secondary->Character->Body;
    if (Primary->ActivePose && Primary->ActivePose->BlinkMorphTargets.Num()==2)
    {
        MaxBlinkLeft=FMath::Max(MaxBlinkLeft,Body->GetMorphTarget(Primary->ActivePose->BlinkMorphTargets[0]));
        MaxBlinkRight=FMath::Max(MaxBlinkRight,Body->GetMorphTarget(Primary->ActivePose->BlinkMorphTargets[1]));
    }
    if (Phase==0)
    {
        if (!Body->GetPhysicsAsset() || Body->GetPhysicsAsset()->SkeletalBodySetups.Num()<20 ||
            Body->GetPhysicsAsset()->ConstraintSetup.Num()<20)
        { Finish(false,TEXT("physics_asset_incomplete")); return; }
        if (Primary->ActivePose) { Primary->ActivePose->bBreathing=false; Primary->ActivePose->bIdle=false; }
        if (Secondary->ActivePose) { Secondary->ActivePose->bBreathing=false; Secondary->ActivePose->bIdle=false; }
        if (!Primary->ActivePose || Primary->ActivePose->BlinkMorphTargets.Num()!=2)
        { Finish(false,TEXT("bilateral_blink_not_configured")); return; }
        Primary->ActivePose->BlinkIntervalSeconds=.6f;
        Phase=-1; PhaseTime=Now;
        return;
    }
    if (Phase==-1)
    {
        if (Now-PhaseTime<.15) return;
        InitialHand=Body->GetBoneLocation(TEXT("lHand"));
        OtherHand=Other->GetBoneLocation(TEXT("lHand"));
        FTransform Goal=Body->GetBoneTransform(Body->GetBoneIndex(TEXT("lHand")));
        Goal.AddToTranslation(FVector(15,0,5));
        if (!Primary->Character->SetIKGoal(TEXT("left_hand"),Goal)) { Finish(false,TEXT("ik_goal_rejected")); return; }
        InitialRoot=Primary->GetActorTransform();
        Phase=1; PhaseTime=Now;
        return;
    }
    if (Phase==1)
    {
        if (Now-PhaseTime<.5) return;
        const double DrivenDistance=(Body->GetBoneLocation(TEXT("lHand"))-InitialHand).Size();
        const double OtherDistance=(Other->GetBoneLocation(TEXT("lHand"))-OtherHand).Size();
        if (DrivenDistance<2 || OtherDistance>.5)
        {
            UE_LOG(LogTemp,Error,TEXT("VAM_STAGE06_IK_CHECK driven_cm=%.6f other_cm=%.6f"),DrivenDistance,OtherDistance);
            Finish(false,TEXT("ik_or_instance_isolation")); return;
        }
        Phase=2; PhaseTime=Now;
    }
    if (Phase==2)
    {
        const double T=FMath::Clamp(Now-PhaseTime,0.,1.5);
        FTransform Next=InitialRoot;
        Next.AddToTranslation(FVector(80*T/1.5,0,0));
        Primary->Motion->MoveContinuously(Next,Now);
        MaxSpeed=FMath::Max(MaxSpeed,float(Primary->Motion->GetMotion().LinearVelocity.Size()));
        for (const FVamInertiaRegion& Region:Primary->Motion->GetInertiaRegions()) MaxWitness=FMath::Max(MaxWitness,float(Region.LocalDisplacement.Size()));
        if (T<1.5) return;
        if (MaxSpeed<5 || MaxWitness<.01)
        { Finish(false,TEXT("continuous_motion_or_inertia")); return; }
        if (!Primary->Interaction->SetPhysicalMode(EVamPhysicalMode::LocalResponse,TEXT("lHand")))
        { Finish(false,TEXT("local_physics_unavailable")); return; }
        const FVector Hand=Body->GetBoneLocation(TEXT("lHand"));
        GrabStart=Hand;
        if (!Primary->Interaction->GrabBone(TEXT("lHand"),Hand))
        { Finish(false,TEXT("physics_grab_unavailable")); return; }
        Primary->Interaction->MoveGrab(Hand+FVector(12,0,0));
        Phase=3; PhaseTime=Now;
        return;
    }
    if (Phase==3)
    {
        if (Now-PhaseTime<.5) return;
        if (!Primary->Interaction->IsGrabbing()) { Finish(false,TEXT("grab_not_retained")); return; }
        if ((Body->GetBoneLocation(TEXT("lHand"))-GrabStart).Size()<1.f)
        { Finish(false,TEXT("grab_body_did_not_move")); return; }
        Primary->Interaction->ReleaseGrab();
        if (Primary->Interaction->IsGrabbing()) { Finish(false,TEXT("release_failed")); return; }
        if (!Primary->Interaction->SetPhysicalMode(EVamPhysicalMode::Ragdoll) || !Body->IsSimulatingPhysics())
        { Finish(false,TEXT("ragdoll_unavailable")); return; }
        Phase=4; PhaseTime=Now;
        return;
    }
    if (Phase==4)
    {
        if (Now-PhaseTime<.5) return;
        if (!Primary->Interaction->SetPhysicalMode(EVamPhysicalMode::Controlled))
        { Finish(false,TEXT("controlled_recovery_failed")); return; }
        FTransform Destination=Primary->GetActorTransform();
        Destination.AddToTranslation(FVector(500,0,0));
        Primary->Motion->TeleportTo(Destination,Now);
        if (!Primary->Motion->GetMotion().bTeleported) { Finish(false,TEXT("teleport_not_classified")); return; }
        for (const FVamInertiaRegion& Region:Primary->Motion->GetInertiaRegions())
            if (Region.LocalDisplacement.Size()>.001) { Finish(false,TEXT("teleport_failed_to_reset_solver")); return; }
        if (MaxBlinkLeft<.65f || MaxBlinkRight<.65f)
        { Finish(false,TEXT("bilateral_blink_not_driven")); return; }
        Finish(true,TEXT("runtime_chain_verified"));
    }
}

#include "VamPhysicsOutputComponent.h"
#include "VamCharacterComponent.h"
#include "VamShapeAnimInstance.h"
#include "VamMotionComponent.h"
#include "Engine/World.h"
#include "Components/SkeletalMeshComponent.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/SkeletalBodySetup.h"
#include "Physics/Experimental/PhysScene_Chaos.h"
#include "Chaos/SimCallbackObject.h"
#include "Chaos/Framework/PhysicsSolverBase.h"
#include "PBDRigidsSolver.h"

struct FVamClockInput : Chaos::FSimCallbackInput
{
    int32 Shape=INDEX_NONE,Pose=INDEX_NONE,Teleport=INDEX_NONE;
    uint64 Generation=0;
    void Reset() { Shape=Pose=Teleport=INDEX_NONE;Generation=0; }
};
struct FVamClockOutput : Chaos::FSimCallbackOutput
{
    int32 Shape=INDEX_NONE,Pose=INDEX_NONE,Teleport=INDEX_NONE;
    double Completed=-1;
    uint64 Generation=0;
    int32 Warmup=0;
    void Reset() { Shape=Pose=Teleport=INDEX_NONE;Completed=-1;Generation=0;Warmup=0; }
};
class FVamPhysicsClockBridge final : public Chaos::TSimCallbackObject<FVamClockInput,FVamClockOutput,Chaos::ESimCallbackOptions::Presimulate | Chaos::ESimCallbackOptions::PostSolve>
{
    int32 LastShape=INDEX_NONE,LastTeleport=INDEX_NONE,CompletedSteps=0;
    uint64 LastGeneration=0;
    virtual void OnPreSimulate_Internal() override {}
    virtual void OnPostSolve_Internal() override
    {
        const auto* Input=GetConsumerInput_Internal(); if(!Input) return;
        if(Input->Shape!=LastShape || Input->Teleport!=LastTeleport || Input->Generation!=LastGeneration)
        { CompletedSteps=0;LastShape=Input->Shape;LastTeleport=Input->Teleport;LastGeneration=Input->Generation; }
        auto& Result=GetProducerOutputData_Internal();
        Result.Shape=Input->Shape;Result.Pose=Input->Pose;Result.Teleport=Input->Teleport;
        Result.Generation=Input->Generation;
        Result.Warmup=++CompletedSteps;
        Result.Completed=GetSimTime_Internal()+GetDeltaTime_Internal();
    }
};
UVamPhysicsOutputComponent::UVamPhysicsOutputComponent()
{
    PrimaryComponentTick.bCanEverTick=true;
    PrimaryComponentTick.TickGroup=TG_PostUpdateWork;
}
void UVamPhysicsOutputComponent::BeginPlay()
{
    Super::BeginPlay();
    if(FPhysScene* Scene=GetWorld()->GetPhysicsScene())
    {
        Bridge=Scene->GetSolver()->CreateAndRegisterSimCallbackObject_External<FVamPhysicsClockBridge>();
        PrePhysicsHandle=Scene->OnPhysScenePreTick.AddUObject(this,&UVamPhysicsOutputComponent::SubmitPhysicsInput);
    }
}
void UVamPhysicsOutputComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    if(FPhysScene* Scene=GetWorld()->GetPhysicsScene())
    {
        Scene->OnPhysScenePreTick.Remove(PrePhysicsHandle);
        if(Bridge) Scene->GetSolver()->UnregisterAndFreeSimCallbackObject_External(Bridge);
    }
    Bridge=nullptr;Output=FVamCollisionOutput();Super::EndPlay(Reason);
}
void UVamPhysicsOutputComponent::SubmitPhysicsInput(FPhysScene_Chaos* Scene,float DeltaTime)
{
    if(!Bridge) return;
    auto* Input=Bridge->GetProducerInputData_External();Input->Reset();
    const auto* C=GetOwner()->FindComponentByClass<UVamCharacterComponent>();
    const auto* Anim=C && C->Body ? Cast<UVamShapeAnimInstance>(C->Body->GetAnimInstance()) : nullptr;
    if(C && Anim)
    {
        Input->Shape=C->CollisionShapeRevision;Input->Pose=Anim->ProducedPoseRevision;
        Input->Generation=C->GetLoadGeneration();
        if(const auto* Motion=GetOwner()->FindComponentByClass<UVamMotionComponent>()) Input->Teleport=Motion->GetClock().TeleportRevision;
    }
}
FVamCollisionOutput UVamPhysicsOutputComponent::GetCollisionOutput() const
{
    FVamCollisionOutput Result=Output;
    const auto* C=GetOwner()->FindComponentByClass<UVamCharacterComponent>();
    const auto* Motion=GetOwner()->FindComponentByClass<UVamMotionComponent>();
    Result.bValid&=C && C->GetLoadGeneration()==Result.CharacterGeneration && C->CollisionShapeRevision==Result.ShapeRevision &&
        (!Motion || Motion->GetClock().TeleportRevision==Result.TeleportRevision);
    return Result;
}
void UVamPhysicsOutputComponent::TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime,TickType,ThisTickFunction);
    Output.bValid=false;
    auto* C=GetOwner()->FindComponentByClass<UVamCharacterComponent>();
    if(!Bridge || !C || !C->Body || !C->Body->GetPhysicsAsset()) return;
    // This queue is released by Chaos at its external results time; never read
    // solver-internal clocks from the game thread or step the world manually.
    while(auto Result=Bridge->PopOutputData_External())
    {
        Output.SolverCompletedTimeSeconds=Result->Completed;
        Output.ShapeRevision=Result->Shape;Output.AnimationPoseRevision=Result->Pose;Output.TeleportRevision=Result->Teleport;
        Output.CharacterGeneration=Result->Generation;
        Output.CompletedStepsSinceRebind=Result->Warmup;
    }
    Output.SolverResultsTimeSeconds=GetWorld()->GetPhysicsScene()->GetSolver()->GetPhysicsResultsTime_External();
    const double Lag=FMath::Abs(Output.SolverResultsTimeSeconds-Output.SolverCompletedTimeSeconds);
    Output.bValid=Output.ShapeRevision!=INDEX_NONE && Output.ShapeRevision==C->CollisionShapeRevision &&
        Output.CharacterGeneration==C->GetLoadGeneration() && Lag<=MaximumBridgeDelaySeconds && Output.CompletedStepsSinceRebind>=WarmupPhysicsSteps;
    if(const auto* Motion=GetOwner()->FindComponentByClass<UVamMotionComponent>()) Output.bValid&=Output.TeleportRevision==Motion->GetClock().TeleportRevision;
    Output.PublishedWorldTimeSeconds=GetWorld()->GetTimeSeconds();Output.Capsules.Reset();
    Output.ProducerInstance=GetOwner()->GetFName();
    FinalPose=C->Body->GetComponentSpaceTransforms();
    if(const auto* Anim=Cast<UVamShapeAnimInstance>(C->Body->GetAnimInstance())) Output.FinalPoseAnimationRevision=Anim->ProducedPoseRevision;
    for(const auto& Setup:C->Body->GetPhysicsAsset()->SkeletalBodySetups)
    {
        const FBodyInstance* BI=C->Body->GetBodyInstance(Setup->BoneName);
        if(!BI || BI->GetCollisionEnabled()==ECollisionEnabled::NoCollision) continue;
        const FTransform BoneWorld=BI->GetUnrealWorldTransform();
        for(const auto& Capsule:Setup->AggGeom.SphylElems)
        {
            FVamCollisionCapsule Value;Value.Bone=Setup->BoneName;
            Value.WorldTransform=FTransform(Capsule.Rotation,Capsule.Center)*BoneWorld;
            const FVector Scale=BoneWorld.GetScale3D().GetAbs();
            Value.Radius=Capsule.Radius*FMath::Max(Scale.X,Scale.Y);Value.HalfLength=Capsule.Length*.5*Scale.Z;
            Output.Capsules.Add(Value);
        }
    }
}

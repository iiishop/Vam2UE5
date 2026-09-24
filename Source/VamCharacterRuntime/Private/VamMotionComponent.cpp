#include "VamMotionComponent.h"
#include "GameFramework/Actor.h"

UVamMotionComponent::UVamMotionComponent()
{
    PrimaryComponentTick.bCanEverTick=true;
    PrimaryComponentTick.TickGroup=TG_PrePhysics;
    FVamInertiaRegion Chest; Chest.Name=TEXT("chest_witness"); Chest.LocalAnchor=FVector(0,0,125);
    FVamInertiaRegion Hip; Hip.Name=TEXT("hip_witness"); Hip.LocalAnchor=FVector(0,0,88); Hip.Stiffness=45; Hip.Damping=11;
    InertiaRegions={Chest,Hip};
}

void UVamMotionComponent::BeginPlay()
{
    Super::BeginPlay();
    if (const AActor* Owner=GetOwner())
    {
        ResetAt(Owner->GetActorTransform(),GetWorld()->GetTimeSeconds());
        OnMotionUpdated.Broadcast(Current);
    }
}

void UVamMotionComponent::ResetAt(const FTransform& Transform, double Time)
{
    Current=FVamMotionSample();
    Current.WorldTransform=Transform;
    Current.TimeSeconds=Time;
    Clock.TimeSeconds=Time;
    Accumulator=0;
    bHaveSample=true;
    for (FVamInertiaRegion& Region:InertiaRegions) { Region.LocalDisplacement=FVector::ZeroVector; Region.LocalVelocity=FVector::ZeroVector; }
}

void UVamMotionComponent::Submit(const FTransform& Transform, double Time, bool bExplicitTeleport)
{
    if (!Transform.IsValid() || !FMath::IsFinite(Time)) return;
    if (!bHaveSample) { ResetAt(Transform,Time); OnMotionUpdated.Broadcast(Current); return; }
    const double Dt=Time-Current.TimeSeconds;
    if (Dt<=UE_DOUBLE_SMALL_NUMBER && !bExplicitTeleport) return;
    const FVector Delta=Transform.GetLocation()-Current.WorldTransform.GetLocation();
    FQuat RotationDelta=Transform.GetRotation()*Current.WorldTransform.GetRotation().Inverse();
    RotationDelta.Normalize();
    FVector Axis; double Angle;
    RotationDelta.ToAxisAndAngle(Axis,Angle);
    if (Angle>PI) Angle-=2*PI;
    const bool bTeleport=bExplicitTeleport || Delta.Size()>TeleportDistanceCm || FMath::Abs(FMath::RadiansToDegrees(Angle))>TeleportAngleDegrees;
    if (bTeleport)
    {
        ResetAt(Transform,Time);
        Current.bTeleported=true;
        ++Clock.TeleportRevision;
        Clock.LastTeleportTimeSeconds=Time;
        Clock.WarmupSteps=4;
        OnMotionUpdated.Broadcast(Current);
        return;
    }
    FVamMotionSample Next;
    Next.TimeSeconds=Time;
    Next.WorldTransform=Transform;
    Next.LinearVelocity=Delta/Dt;
    Next.AngularVelocity=Axis.GetSafeNormal()*Angle/Dt;
    Next.LinearAcceleration=(Next.LinearVelocity-Current.LinearVelocity)/Dt;
    Next.AngularAcceleration=(Next.AngularVelocity-Current.AngularVelocity)/Dt;
    Current=Next;
    OnMotionUpdated.Broadcast(Current);
}

void UVamMotionComponent::MoveContinuously(const FTransform& Transform, double TimestampSeconds)
{
    if (!GetOwner() || !Transform.IsValid()) return;
    GetOwner()->SetActorTransform(Transform,false,nullptr,ETeleportType::None);
    Submit(Transform,TimestampSeconds,false);
}

void UVamMotionComponent::TeleportTo(const FTransform& Transform, double TimestampSeconds)
{
    if (!GetOwner() || !Transform.IsValid()) return;
    GetOwner()->SetActorTransform(Transform,false,nullptr,ETeleportType::TeleportPhysics);
    Submit(Transform,TimestampSeconds,true);
}

void UVamMotionComponent::SetPreviewPaused(bool bPause) { Clock.bPaused=bPause; }
void UVamMotionComponent::StepPreview()
{
    if (!Clock.bPaused) return;
    Clock.LastSteps=1;
    Clock.TimeSeconds+=FixedStepSeconds;
    AdvanceRegions(FixedStepSeconds);
    OnSolverStep.Broadcast(FixedStepSeconds);
}
void UVamMotionComponent::ResetPreview()
{
    const bool bPause=Clock.bPaused;
    Clock=FVamSolverClock();
    Clock.bPaused=bPause;
    Clock.FixedStepSeconds=FixedStepSeconds;
    if (GetOwner()) { ResetAt(GetOwner()->GetActorTransform(),GetWorld()->GetTimeSeconds()); OnMotionUpdated.Broadcast(Current); }
    Clock.WarmupSteps=4;
}
void UVamMotionComponent::CommitShapeRevision(int32 Revision)
{
    Clock.ShapeRevision=Revision;
    Clock.WarmupSteps=4;
    Clock.LastShapeCommitTimeSeconds=GetWorld() ? GetWorld()->GetTimeSeconds() : Clock.TimeSeconds;
}

void UVamMotionComponent::AdvanceRegions(float StepSeconds)
{
    const FQuat Inverse=Current.WorldTransform.GetRotation().Inverse();
    const FVector LinearAcceleration=Inverse.RotateVector(Current.LinearAcceleration);
    const FVector AngularAcceleration=Inverse.RotateVector(Current.AngularAcceleration);
    const FVector AngularVelocity=Inverse.RotateVector(Current.AngularVelocity);
    for (FVamInertiaRegion& Region:InertiaRegions)
    {
        const FVector InertialForce=(-LinearAcceleration
            -FVector::CrossProduct(AngularAcceleration,Region.LocalAnchor)
            -FVector::CrossProduct(AngularVelocity,FVector::CrossProduct(AngularVelocity,Region.LocalAnchor)))
            .GetClampedToMaxSize(FMath::Max(0.f,MaxProbeAcceleration))*FMath::Max(0.f,Region.InertiaGain);
        const FVector Acceleration=InertialForce-FMath::Max(0.f,Region.Stiffness)*Region.LocalDisplacement
            -FMath::Max(0.f,Region.Damping)*Region.LocalVelocity;
        Region.LocalVelocity+=Acceleration*StepSeconds;
        Region.LocalDisplacement+=Region.LocalVelocity*StepSeconds;
        Region.LocalDisplacement=Region.LocalDisplacement.GetClampedToMaxSize(25.f);
    }
}

void UVamMotionComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime,TickType,ThisTickFunction);
    if (!GetOwner() || !GetWorld()) return;
    Submit(GetOwner()->GetActorTransform(),GetWorld()->GetTimeSeconds(),false);
    AdvanceSolverClock(DeltaTime);
}

void UVamMotionComponent::AdvanceSolverClock(float DeltaTime)
{
    Clock.LastSteps=0;
    Clock.FixedStepSeconds=FixedStepSeconds;
    if (Clock.bPaused || FixedStepSeconds<=0) return;
    const double MaxFrame=FMath::Max(1,MaxStepsPerFrame)*FixedStepSeconds;
    if (DeltaTime>MaxFrame) Clock.DroppedSteps+=FMath::FloorToInt((DeltaTime-MaxFrame)/FixedStepSeconds);
    Accumulator+=FMath::Clamp<double>(DeltaTime,0,MaxFrame);
    while (Accumulator+UE_DOUBLE_SMALL_NUMBER>=FixedStepSeconds && Clock.LastSteps<MaxStepsPerFrame)
    {
        Accumulator-=FixedStepSeconds;
        Clock.TimeSeconds+=FixedStepSeconds;
        ++Clock.LastSteps;
        if (Clock.WarmupSteps>0) --Clock.WarmupSteps;
        AdvanceRegions(FixedStepSeconds);
        OnSolverStep.Broadcast(FixedStepSeconds);
    }
    Clock.InterpolationAlpha=FMath::Clamp<float>(Accumulator/FixedStepSeconds,0,1);
}

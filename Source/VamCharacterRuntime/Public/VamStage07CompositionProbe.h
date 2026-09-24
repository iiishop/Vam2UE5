#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "VamStage07CompositionProbe.generated.h"

/** Narrow Play-world composition regression; not the Stage07 baseline/soft-tissue gate. */
UCLASS()
class VAMCHARACTERRUNTIME_API AVamStage07CompositionProbe : public AActor
{
    GENERATED_BODY()
public:
    AVamStage07CompositionProbe();
    UPROPERTY(EditAnywhere, Category="Test") TArray<TObjectPtr<class AVamCharacterActor>> Subjects;
    UPROPERTY(EditAnywhere, Category="Test") float FinalIKToleranceCm=3.f;
    UPROPERTY(EditAnywhere, Category="Test") float MinimumBaseMotionCm=.01f;
    UPROPERTY(EditAnywhere, Category="Test") float MinimumGrabMotionCm=1.f;
    UPROPERTY(EditAnywhere, Category="Test") float FinalFootToleranceCm=3.f;
    UPROPERTY(EditAnywhere, Category="Test") float SettledSpeedToleranceCmS=2.f;
    UPROPERTY(EditAnywhere, Category="Test") float SettledDriftToleranceCm=.5f;
    UPROPERTY(EditAnywhere, Category="Test") float JointLimitToleranceDegrees=2.f;
protected:
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
private:
    struct FSample
    {
        FName LeftHand, RightHand;
        FTransform Goal;
        FVector GrabStart=FVector::ZeroVector;
        TArray<FTransform> InitialPose;
        double PreviousClock=0, ClockTravel=0;
        float BaseMotion=0, IKError=0, Blink=0, Breath=0, GrabMotion=0, ReleaseVelocityDelta=0;
        int32 ShapeTransactions=0;
        float ShapeVelocityDelta=0;
        float FootError=0,ColliderChange=0;
        double MaximumPhysicsDelay=0;
        FName ShapeParameter;
        double WitnessTime=0,PhysicsTime=0;
        FTransform MotionStart;
        FVector SettledPosition=FVector::ZeroVector;
        float SettledSpeed=0,SettledDrift=0;
        float JointLimitError=0;
    };
    TArray<FSample> Samples;
    bool bEnabled=false;
    int32 Phase=0;
    int32 ShapeCycle=0;
    double Started=0, PhaseStarted=0;
    double PausedWallTime=0;
    bool TimingPassed=false;
    void Finish(bool Passed,const FString& Reason);
    void TickTiming(double Now);
};

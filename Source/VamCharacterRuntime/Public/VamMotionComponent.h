#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "VamMotionComponent.generated.h"

USTRUCT(BlueprintType)
struct VAMCHARACTERRUNTIME_API FVamMotionSample
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly, Category="VaM|Motion|Sample") double TimeSeconds = 0;
    UPROPERTY(BlueprintReadOnly, Category="VaM|Motion|Sample") FTransform WorldTransform;
    UPROPERTY(BlueprintReadOnly, Category="VaM|Motion|Sample") FVector LinearVelocity = FVector::ZeroVector;
    UPROPERTY(BlueprintReadOnly, Category="VaM|Motion|Sample") FVector AngularVelocity = FVector::ZeroVector;
    UPROPERTY(BlueprintReadOnly, Category="VaM|Motion|Sample") FVector LinearAcceleration = FVector::ZeroVector;
    UPROPERTY(BlueprintReadOnly, Category="VaM|Motion|Sample") FVector AngularAcceleration = FVector::ZeroVector;
    UPROPERTY(BlueprintReadOnly, Category="VaM|Motion|Sample") bool bTeleported = false;
};

USTRUCT(BlueprintType)
struct VAMCHARACTERRUNTIME_API FVamSolverClock
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly, Category="VaM|Motion|Clock") double TimeSeconds = 0;
    UPROPERTY(BlueprintReadOnly, Category="VaM|Motion|Clock") float FixedStepSeconds = 1.f/120.f;
    UPROPERTY(BlueprintReadOnly, Category="VaM|Motion|Clock") int32 LastSteps = 0;
    UPROPERTY(BlueprintReadOnly, Category="VaM|Motion|Clock") int32 DroppedSteps = 0;
    UPROPERTY(BlueprintReadOnly, Category="VaM|Motion|Clock") float InterpolationAlpha = 0;
    UPROPERTY(BlueprintReadOnly, Category="VaM|Motion|Clock") bool bPaused = false;
    UPROPERTY(BlueprintReadOnly, Category="VaM|Motion|Clock") int32 ShapeRevision = 0;
    UPROPERTY(BlueprintReadOnly, Category="VaM|Motion|Clock") int32 WarmupSteps = 0;
    UPROPERTY(BlueprintReadOnly, Category="VaM|Motion|Clock") int32 TeleportRevision = 0;
    UPROPERTY(BlueprintReadOnly, Category="VaM|Motion|Clock") double LastTeleportTimeSeconds = -1;
    UPROPERTY(BlueprintReadOnly, Category="VaM|Motion|Clock") double LastShapeCommitTimeSeconds = -1;
};

/** Low-cost Stage06 inertial witness, not a surface/soft-body solver. */
USTRUCT(BlueprintType)
struct VAMCHARACTERRUNTIME_API FVamInertiaRegion
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VaM|Motion|Inertia") FName Name;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VaM|Motion|Inertia") FVector LocalAnchor = FVector::ZeroVector;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VaM|Motion|Inertia") float Stiffness = 70.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VaM|Motion|Inertia") float Damping = 14.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VaM|Motion|Inertia") float InertiaGain = 0.1f;
    UPROPERTY(BlueprintReadOnly, Category="VaM|Motion|Inertia") FVector LocalDisplacement = FVector::ZeroVector;
    UPROPERTY(BlueprintReadOnly, Category="VaM|Motion|Inertia") FVector LocalVelocity = FVector::ZeroVector;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FVamMotionUpdated, const FVamMotionSample&, Sample);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FVamSolverStep, float, StepSeconds);

/** Actor motion and fixed-time solver feed. Each instance owns its own history. */
UCLASS(ClassGroup=(VaM), meta=(BlueprintSpawnableComponent))
class VAMCHARACTERRUNTIME_API UVamMotionComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UVamMotionComponent();
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VaM|Motion", meta=(ClampMin="0.001")) float FixedStepSeconds = 1.f/120.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VaM|Motion", meta=(ClampMin="1",ClampMax="16")) int32 MaxStepsPerFrame = 8;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VaM|Motion") float TeleportDistanceCm = 150.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VaM|Motion") float TeleportAngleDegrees = 100.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VaM|Motion") float MaxProbeAcceleration = 2500.f;
    UPROPERTY(BlueprintAssignable, Category="VaM|Motion") FVamMotionUpdated OnMotionUpdated;
    UPROPERTY(BlueprintAssignable, Category="VaM|Motion") FVamSolverStep OnSolverStep;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VaM|Motion") TArray<FVamInertiaRegion> InertiaRegions;
    UFUNCTION(BlueprintCallable, Category="VaM|Motion") void SetPreviewPaused(bool bPause);
    UFUNCTION(BlueprintCallable, Category="VaM|Motion") void StepPreview();
    UFUNCTION(BlueprintCallable, Category="VaM|Motion") void ResetPreview();
    UFUNCTION(BlueprintCallable, Category="VaM|Motion") void MoveContinuously(const FTransform& WorldTransform, double TimestampSeconds);
    UFUNCTION(BlueprintCallable, Category="VaM|Motion") void TeleportTo(const FTransform& WorldTransform, double TimestampSeconds);
    UFUNCTION(BlueprintCallable, Category="VaM|Motion") void CommitShapeRevision(int32 Revision);
    UFUNCTION(BlueprintCallable, Category="VaM|Motion") void AdvanceSolverClock(float DeltaTime);
    UFUNCTION(BlueprintPure, Category="VaM|Motion") FVamMotionSample GetMotion() const { return Current; }
    UFUNCTION(BlueprintPure, Category="VaM|Motion") FVamSolverClock GetClock() const { return Clock; }
    UFUNCTION(BlueprintPure, Category="VaM|Motion") TArray<FVamInertiaRegion> GetInertiaRegions() const { return InertiaRegions; }
protected:
    virtual void BeginPlay() override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
private:
    void Submit(const FTransform& WorldTransform, double TimestampSeconds, bool bExplicitTeleport);
    void ResetAt(const FTransform& WorldTransform, double TimestampSeconds);
    void AdvanceRegions(float StepSeconds);
    UPROPERTY(Transient) FVamMotionSample Current;
    UPROPERTY(Transient) FVamSolverClock Clock;
    double Accumulator = 0;
    bool bHaveSample = false;
};

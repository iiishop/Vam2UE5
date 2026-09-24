#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "VamPhysicsOutputComponent.generated.h"

USTRUCT(BlueprintType)
struct FVamCollisionCapsule
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly, Category="VaM") FName Bone;
    UPROPERTY(BlueprintReadOnly, Category="VaM") FTransform WorldTransform;
    UPROPERTY(BlueprintReadOnly, Category="VaM") float Radius=0;
    UPROPERTY(BlueprintReadOnly, Category="VaM") float HalfLength=0;
};
/** CPU capsule representation; explicitly not a deformed skin or volumetric tissue output. */
USTRUCT(BlueprintType)
struct FVamCollisionOutput
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly, Category="VaM") int32 SchemaVersion=1;
    UPROPERTY(BlueprintReadOnly, Category="VaM") FName Producer=TEXT("ChaosRigidCapsules");
    UPROPERTY(BlueprintReadOnly, Category="VaM") FName ProducerInstance;
    UPROPERTY(BlueprintReadOnly, Category="VaM") FName Space=TEXT("WorldCm");
    UPROPERTY(BlueprintReadOnly, Category="VaM") bool bValid=false;
    UPROPERTY(BlueprintReadOnly, Category="VaM") double SolverCompletedTimeSeconds=-1;
    UPROPERTY(BlueprintReadOnly, Category="VaM") double SolverResultsTimeSeconds=-1;
    UPROPERTY(BlueprintReadOnly, Category="VaM") double PublishedWorldTimeSeconds=-1;
    UPROPERTY(BlueprintReadOnly, Category="VaM") int32 ShapeRevision=INDEX_NONE;
    UPROPERTY(BlueprintReadOnly, Category="VaM") int64 CharacterGeneration=0;
    UPROPERTY(BlueprintReadOnly, Category="VaM") int32 AnimationPoseRevision=INDEX_NONE;
    UPROPERTY(BlueprintReadOnly, Category="VaM") int32 FinalPoseAnimationRevision=INDEX_NONE;
    UPROPERTY(BlueprintReadOnly, Category="VaM") int32 TeleportRevision=INDEX_NONE;
    UPROPERTY(BlueprintReadOnly, Category="VaM") int32 CompletedStepsSinceRebind=0;
    UPROPERTY(BlueprintReadOnly, Category="VaM") TArray<FVamCollisionCapsule> Capsules;
};

UCLASS(ClassGroup=(VaM), meta=(BlueprintSpawnableComponent))
class VAMCHARACTERRUNTIME_API UVamPhysicsOutputComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UVamPhysicsOutputComponent();
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VaM") double MaximumBridgeDelaySeconds=.1;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VaM") int32 WarmupPhysicsSteps=4;
    UFUNCTION(BlueprintPure, Category="VaM") FVamCollisionOutput GetCollisionOutput() const;
    const TArray<FTransform>& GetFinalPose() const { return FinalPose; }
protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
private:
    void SubmitPhysicsInput(class FPhysScene_Chaos* Scene,float DeltaTime);
    class FVamPhysicsClockBridge* Bridge=nullptr;
    FDelegateHandle PrePhysicsHandle;
    FVamCollisionOutput Output;
    TArray<FTransform> FinalPose;
};

#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "VamActivePoseComponent.generated.h"

/** Intentional pose motion only. Never writes ShapeState or passive inertia. */
UCLASS(ClassGroup=(VaM), meta=(BlueprintSpawnableComponent))
class VAMCHARACTERRUNTIME_API UVamActivePoseComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UVamActivePoseComponent();
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VaM|ActivePose") bool bBreathing=true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VaM|ActivePose") float BreathsPerMinute=14.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VaM|ActivePose") float BreathDepth=1.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VaM|ActivePose") float BreathPhase=0.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VaM|ActivePose") bool bIdle=true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VaM|ActivePose") bool bBlink=true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VaM|ActivePose") FName BlinkMorphTarget;
    /** Source mapped left/right eyelid morphs; evaluated together for a blink. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VaM|ActivePose") TArray<FName> BlinkMorphTargets;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VaM|ActivePose") float BlinkIntervalSeconds=4.f;
    /** Active eyelid closure target for a later facial solver, even when this mesh has no blink morph. */
    UPROPERTY(BlueprintReadOnly, Transient, Category="VaM|ActivePose") float BlinkWeight=0.f;
    UPROPERTY(BlueprintReadOnly, Transient, Category="VaM|ActivePose") float BreathValue=0.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VaM|ActivePose") FVector2D EyeGazeDegrees=FVector2D::ZeroVector;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VaM|ActivePose") float JawOpenDegrees=0.f;
protected:
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
};

#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "VamBodyContactResponseComponent.generated.h"

/** Force-driven, bounded animation response. Independent of volumetric contact enablement. */
UCLASS(ClassGroup=(VaM),meta=(BlueprintSpawnableComponent))
class VAMCHARACTERRUNTIME_API UVamBodyContactResponseComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UVamBodyContactResponseComponent();
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="VaM|Body Contact") bool bEnabled=true;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="VaM|Body Contact") bool bRigidProxyContact=true;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="VaM|Body Contact") float ResponseFrequencyHz=3;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="VaM|Body Contact") float DampingRatio=1;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="VaM|Body Contact") float MaximumOffsetCm=6;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="VaM|Body Contact") float MaximumRotationDegrees=20;
    /** Limits the additive pose slew, not the Jiggle amplitude or contact force. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="VaM|Body Contact",meta=(ClampMin="0")) float MaximumLinearSpeedCmPerSecond=20;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="VaM|Body Contact",meta=(ClampMin="0")) float MaximumAngularSpeedDegreesPerSecond=60;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="VaM|Body Contact") float MaximumForceNewtons=1000;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="VaM|Body Contact") float ProxyStiffnessNewtonsPerCm=30;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="VaM|Body Contact") FVector AppliedForceNewtons=FVector::ZeroVector;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="VaM|Body Contact") FString Status;
    /** Force in newtons, position in world cm. Consumed once on the next response tick. */
    UFUNCTION(BlueprintCallable,Category="VaM|Body Contact") void AddContactForce(FVector ForceNewtons,FVector WorldPoint);
    UFUNCTION(BlueprintCallable,Category="VaM|Body Contact") void AddContactWrench(FVector ForceNewtons,FVector TorqueNewtonMeters,FVector WorldOrigin);
    UFUNCTION(BlueprintCallable,Category="VaM|Body Contact") void ResetResponse();
    const TMap<int32,FTransform>& GetOffsets() const { return Offsets; }
    virtual void TickComponent(float Dt,ELevelTick Tick,FActorComponentTickFunction* Function) override;
private:
    struct FLoad { FVector Force,Point; FVector Torque=FVector::ZeroVector; };
    TArray<FLoad> Pending;
    TMap<int32,FTransform> Offsets;
    TMap<int32,FVector> PreviousCenters;
    TWeakObjectPtr<class USkeletalMeshComponent> LastBody;
    FVector Position=FVector::ZeroVector,Velocity=FVector::ZeroVector;
    FVector Angle=FVector::ZeroVector,AngularVelocity=FVector::ZeroVector;
    int32 LastShape=INDEX_NONE,LastTeleport=INDEX_NONE;
};

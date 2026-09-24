#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "VamInteractionComponent.generated.h"

UENUM(BlueprintType)
enum class EVamPhysicalMode : uint8 { Controlled, LocalResponse, Ragdoll };

UCLASS(ClassGroup=(VaM), meta=(BlueprintSpawnableComponent))
class VAMCHARACTERRUNTIME_API UVamInteractionComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UVamInteractionComponent();
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VaM|Physics") float DriveStrength = 400.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VaM|Physics") float DriveDamping = 50.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VaM|Physics") float PhysicsBlend = 0.5f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VaM|Physics") bool bGravity = true;
    UPROPERTY(BlueprintReadOnly, Category="VaM|Physics") EVamPhysicalMode Mode = EVamPhysicalMode::Controlled;
    UFUNCTION(BlueprintCallable, Category="VaM|Physics") bool SetPhysicalMode(EVamPhysicalMode NewMode, FName LocalRootBone = NAME_None);
    /** Physical response in both root child branches while the actor root stays kinematic for continuous dragging. */
    UFUNCTION(BlueprintCallable, Category="VaM|Physics") bool SetRootMotionResponse(FName LowerRootBone, FName UpperRootBone);
    UFUNCTION(BlueprintCallable, Category="VaM|Physics") bool GrabBone(FName Bone, FVector WorldLocation);
    UFUNCTION(BlueprintCallable, Category="VaM|Physics") void MoveGrab(FVector WorldLocation);
    UFUNCTION(BlueprintCallable, Category="VaM|Physics") void ReleaseGrab();
    UFUNCTION(BlueprintPure, Category="VaM|Physics") bool IsGrabbing() const { return !GrabbedBone.IsNone(); }
    // Called around a synchronous instance-only physics recreation. Mode, roots,
    // target and local grab anchor remain owned by this component.
    void SuspendForShapeRebind();
    void ResumeAfterShapeRebind();
protected:
    virtual void BeginPlay() override;
private:
    UPROPERTY(Transient) TObjectPtr<class UPhysicsHandleComponent> Handle;
    UPROPERTY(Transient) TObjectPtr<class UPhysicalAnimationComponent> PhysicalAnimation;
    UPROPERTY(Transient) FName GrabbedBone;
    UPROPERTY(Transient) FName LocalRoot;
    FVector GrabLocalAnchor=FVector::ZeroVector;
    FVector SavedGrabTarget=FVector::ZeroVector;
    FRotator SavedGrabRotation=FRotator::ZeroRotator;
};

#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "VamHairProfile.h"
#include "VamHairComponent.generated.h"

UCLASS(ClassGroup=(VaM), meta=(BlueprintSpawnableComponent))
class VAMCHARACTERRUNTIME_API UVamHairComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UVamHairComponent();
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VaM|Hair") bool bEnabled=true;
    /** Restoration is inspected without physics first. Coarse/Fine are independent of this switch. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VaM|Hair") bool bSimulate=false;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VaM|Hair") TSoftObjectPtr<UVamHairProfile> Profile;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VaM|Hair") EVamHairQuality Quality=EVamHairQuality::Fine;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="VaM|Hair") TObjectPtr<class UGroomComponent> Groom;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="VaM|Hair") FString Status;
    UFUNCTION(BlueprintCallable, Category="VaM|Hair") void SetHairProfile(UVamHairProfile* InProfile);
    UFUNCTION(BlueprintCallable, Category="VaM|Hair") void SetHairQuality(EVamHairQuality InQuality);
    UFUNCTION(BlueprintCallable, Category="VaM|Hair") void ResetHairSimulation();
    UFUNCTION(BlueprintCallable, Category="VaM|Hair") void SetHairEnabled(bool InEnabled);
    UFUNCTION(BlueprintCallable, Category="VaM|Hair") void SetHairSimulationEnabled(bool InEnabled);
protected:
    virtual void TickComponent(float Dt,ELevelTick Type,FActorComponentTickFunction* Tick) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
    TWeakObjectPtr<class USkeletalMeshComponent> BoundBody;
    UPROPERTY(Transient) TObjectPtr<UVamHairProfile> AppliedProfile;
    EVamHairQuality AppliedQuality=EVamHairQuality::Coarse;
    void ReleaseGroom();
};

#pragma once
#include "CoreMinimal.h"
#include "Components/SkeletalMeshComponent.h"
#include "VamBreastSolver.h"
#include "VamBreastSkeletalMeshComponent.generated.h"

/** Native GPU skeletal skinning; the hook runs after rigid blending, before buffer publication. */
UCLASS()
class VAMCHARACTERRUNTIME_API UVamBreastSkeletalMeshComponent : public USkeletalMeshComponent
{
    GENERATED_BODY()
public:
    UPROPERTY(Transient) TObjectPtr<UVamBreastJiggleProfile> BreastProfile;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VaM|Breast Jiggle") bool bJiggleEnabled=true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VaM|Breast Jiggle",meta=(ClampMin="0.2",ClampMax="100",UIMax="20")) double Softness=1;
    UPROPERTY(BlueprintReadOnly, Transient, Category="VaM|Breast Jiggle") double DensityOverrideKgPerCm3=0;
    UFUNCTION(BlueprintCallable, Category="VaM|Breast Jiggle") void SetBreastDensity(double DensityKgPerCm3);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VaM|Breast Jiggle",meta=(ClampMin="0.2",ClampMax="3")) FVector FrequencyScale=FVector(1);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VaM|Breast Jiggle",meta=(ClampMin="0.05",ClampMax="4")) FVector DampingScale=FVector(1);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VaM|Breast Jiggle",meta=(ClampMin="0.25",ClampMax="4")) FVector TravelScale=FVector(1);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VaM|Breast Jiggle",meta=(ClampMin="0",ClampMax="2")) double CouplingScale=1;
    UFUNCTION(BlueprintCallable, Category="VaM|Breast Jiggle") void ApplyBreastTuningPreset(FName Preset);
    FVamBreastTuning GetBreastTuning() const;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VaM|Breast Jiggle") bool bShowHelperBones=false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VaM|Breast Jiggle") bool bShowRegionWeights=false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VaM|Breast Jiggle") bool bShowDynamicNodes=false;
    UFUNCTION(BlueprintCallable, Category="VaM|Breast Jiggle") void ResetBreastJiggle();
    UFUNCTION(BlueprintPure, Category="VaM|Breast Jiggle") FString BreastDiagnostics() const;
    UFUNCTION(BlueprintCallable, Category="VaM|Breast Jiggle") void BreastMotionCommand(FName Command);
    virtual void TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* TickFunction) override;
    void UpdateBreastShape(const TMap<FName,float>& Values,TArray<FTransform>& Reference,bool Committed);
    virtual void FinalizeBoneTransform() override;
    TArray<FVamBreastSolver> Solvers;
    TArray<FVamBreastSideProfile> RestSides;
private:
    FName DebugCommand;
    FVector DebugVelocity=FVector::ZeroVector,DebugDirection=FVector::ZeroVector;
    double DebugTime=0;
    double LastTime=-1;
    int32 LastTeleport=INDEX_NONE;
    bool bRebase=true,bWasEnabled=true;
};

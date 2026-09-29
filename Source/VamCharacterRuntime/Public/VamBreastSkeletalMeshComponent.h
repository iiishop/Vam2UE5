#pragma once
#include "CoreMinimal.h"
#include "Components/SkeletalMeshComponent.h"
#include "VamBreastSolver.h"
#include "VamBreastDebugTrajectory.h"
#include "VamBreastSkeletalMeshComponent.generated.h"

/** Native GPU skeletal skinning; the hook runs after rigid blending, before buffer publication. */
UCLASS()
class VAMCHARACTERRUNTIME_API UVamBreastSkeletalMeshComponent : public USkeletalMeshComponent
{
    GENERATED_BODY()
public:
    UPROPERTY(Transient) TObjectPtr<UVamBreastJiggleProfile> BreastProfile;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VaM|Breast Jiggle") bool bJiggleEnabled=true;
    /** Visible secondary-motion multiplier; 1 preserves the original output. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VaM|Breast Jiggle",meta=(ClampMin="0",ClampMax="10")) double BreastAmplitude=2;
    // Legacy fields retained for schema-1 assets only; absent from the formal tuning UI.
    UPROPERTY() double Softness=1;
    UPROPERTY(Transient) double DensityOverrideKgPerCm3=0;
    UFUNCTION(BlueprintCallable, Category="VaM|Legacy",meta=(DeprecatedFunction,DeprecationMessage="Use calibrated Mass Scale; Density is internal")) void SetBreastDensity(double DensityKgPerCm3);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VaM|Breast Jiggle",meta=(ClampMin="0.1",ClampMax="10")) double Support=1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VaM|Breast Jiggle",meta=(ClampMin="0.1",ClampMax="4")) double Damping=1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VaM|Breast Jiggle",meta=(DisplayName="Mobility",ClampMin="0.25",ClampMax="3")) double BreastMobility=1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VaM|Breast Jiggle",meta=(ClampMin="0",ClampMax="4")) double InternalCoupling=1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category="VaM|Breast Jiggle",meta=(ClampMin="0.1",ClampMax="10")) double MassScale=1;
    UFUNCTION(BlueprintCallable, Category="VaM|Breast Jiggle") void ResetBreastTuning();
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
    FVamMotionRamp DebugLinear[3],DebugYaw;
    FVamJumpTrajectory DebugJump;
    double DebugLinearTime=0,DebugYawTime=0,DebugJumpTime=0;
    bool bDebugLinear=false,bDebugYaw=false,bDebugJump=false;
    double LastTime=-1;
    int32 LastTeleport=INDEX_NONE;
    bool bRebase=true,bWasEnabled=true;
};

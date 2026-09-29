#pragma once
#include "VamBreastSkeletalMeshComponent.h"
#include "VamGluteStructure.h"
#include "VamGluteCorrectiveProfile.h"
#include "VamGluteSolver.h"
#include "VamGluteSkeletalMeshComponent.generated.h"

UCLASS()
class VAMCHARACTERRUNTIME_API UVamGluteSkeletalMeshComponent : public UVamBreastSkeletalMeshComponent
{
    GENERATED_BODY()
public:
    UPROPERTY(Transient) TObjectPtr<UVamGluteJiggleProfile> GluteJiggleProfile;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VaM|Glute Jiggle") bool bGluteJiggleEnabled=true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VaM|Glute Jiggle") double GluteSupport=.45;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VaM|Glute Jiggle") double GluteDamping=.65;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VaM|Glute Jiggle") double GluteMobility=2.;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VaM|Glute Jiggle") double GluteInternalCoupling=1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category="VaM|Glute Jiggle") double GluteMassScale=1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VaM|Glute Jiggle") bool bShowGluteDynamicNodes=false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VaM|Glute Jiggle") bool bShowGluteDynamicPelvis=false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VaM|Glute Jiggle") bool bShowGluteDynamicThigh=false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VaM|Glute Jiggle") bool bShowGluteVelocity=false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VaM|Glute Jiggle") bool bShowGluteRestDynamic=false;
    UFUNCTION(BlueprintCallable, Category="VaM|Glute Jiggle") void ResetGluteJiggle();
    UFUNCTION(BlueprintCallable, Category="VaM|Glute Jiggle") void GluteMotionCommand(FName Command);
    UFUNCTION(BlueprintPure, Category="VaM|Glute Jiggle") FString GluteJiggleDiagnostics() const;
    FVamGluteTuning GetGluteTuning() const;
    FVamGluteSolver GluteSolvers[2];
    TArray<FVamGluteDynamicSide> GluteDynamics;
    void ApplyGluteJiggle();
    virtual void TickComponent(float Dt,ELevelTick Type,FActorComponentTickFunction* Tick) override;
    UPROPERTY(Transient) TObjectPtr<UVamGluteStructureProfile> GluteProfile;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VaM|Glute Structure") bool bGluteEnabled=true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VaM|Glute Structure") bool bShowGluteRegion=false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VaM|Glute Structure") bool bShowStructuralBones=false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VaM|Glute Structure") bool bShowPelvisAttachments=false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VaM|Glute Structure") bool bShowThighAttachments=false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VaM|Glute Structure") bool bShowPoseTension=false;
    UFUNCTION(BlueprintPure, Category="VaM|Glute Structure") FString GluteDiagnostics() const;
    UFUNCTION(BlueprintCallable, Category="VaM|Glute Structure") void GlutePoseCommand(FName Command);
    void UpdateGluteShape(const TMap<FName,float>& Values,TArray<FTransform>& Reference,int32 ShapeRevision=0);
    UPROPERTY(Transient, BlueprintReadOnly, Category="VaM|Glute Structure") FVamHipPoseState HipPoseState;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VaM|Glute Structure") int32 DebugGluteSide=-1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VaM|Glute Structure") bool bShowFoldSemantics=false;
    virtual void FinalizeBoneTransform() override;
    TArray<FVamGluteSide> GluteRest;
    UPROPERTY(Transient, BlueprintReadOnly, Category="VaM|Glute Structure") TArray<FVamGluteStructuralState> GluteStates;
    int32 GluteShapeRevision=0;
    UPROPERTY(Transient) TObjectPtr<UVamGluteCorrectiveProfile> CorrectiveProfile;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VaM|Glute Corrective") bool bCorrectiveEnabled=true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VaM|Glute Corrective") bool bShowCorrectiveDelta=false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VaM|Glute Corrective") int32 CorrectiveDiagnosticTarget=3;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VaM|Glute Corrective") int32 CorrectiveDiagnosticStage=3;
    /** G06 final-rest surface = G05 transforms plus these native morph weights. */
    UPROPERTY(Transient, BlueprintReadOnly, Category="VaM|Glute Corrective") TMap<FName,float> CorrectiveWeights;
    TArray<TArray<double>> CorrectiveTargetWeights;
    TArray<double> CorrectiveRegionalBounds;
    double CorrectiveMagnitudeBound=0;
    void ApplyGluteCorrectives();
    float AppliedCorrectiveWeight(FName Morph) const;
private:
    double GluteLastTime=-1,GluteWalkTime=0;
    int32 GluteLastTeleport=INDEX_NONE;
    bool bGluteWasEnabled=true,bGluteShapeRebase=false,bGluteWalking=false;
};

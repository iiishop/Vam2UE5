#pragma once
#include "VamGluteSkeletalMeshComponent.h"
#include "VamLegJiggleProfile.h"
#include "VamLegSkeletalMeshComponent.generated.h"

UCLASS()
class VAMCHARACTERRUNTIME_API UVamLegSkeletalMeshComponent : public UVamGluteSkeletalMeshComponent
{
    GENERATED_BODY()
public:
    UPROPERTY(Transient) TObjectPtr<UVamLegJiggleProfile> LegProfile;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VaM|Leg Jiggle") bool bLegJiggleEnabled=true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VaM|Leg Jiggle") double ThighAmplitude=3;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VaM|Leg Jiggle") double CalfAmplitude=3;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VaM|Leg Jiggle") double LegSupport=1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VaM|Leg Jiggle") double LegDamping=1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VaM|Leg Jiggle") bool bShowLegNodes=false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VaM|Leg Jiggle") bool bShowLegRegion=false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VaM|Leg Jiggle") bool bShowLegTension=false;
    UFUNCTION(BlueprintCallable, Category="VaM|Leg Jiggle") void LegPoseCommand(FName Command);
    UFUNCTION(BlueprintCallable, Category="VaM|Leg Jiggle") void ResetLegJiggle();
    UFUNCTION(BlueprintPure, Category="VaM|Leg Jiggle") FString LegDiagnostics() const;
    void UpdateLegShape(const TMap<FName,float>& Values,TArray<FTransform>& Reference);
    virtual void FinalizeBoneTransform() override;
    TArray<FVamLegSegment> LegRest;
    FVamGluteSolver LegSolvers[4];
    TArray<double> LegTension[4];
    FVector LegAngles[4]={};
private:
    double LegLastTime=-1;
    int32 LegLastTeleport=INDEX_NONE;
    bool bLegShapeRebase=false,bLegWasEnabled=true;
};

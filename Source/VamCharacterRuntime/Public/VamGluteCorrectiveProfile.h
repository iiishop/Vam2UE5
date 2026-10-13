#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "VamHipPoseState.h"
#include "VamGluteCorrectiveProfile.generated.h"

USTRUCT(BlueprintType)
struct VAMCHARACTERRUNTIME_API FVamGlutePoseTarget
{
    GENERATED_BODY()
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Corrective") FName Name;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Corrective") FVector Degrees=FVector::ZeroVector;
};
USTRUCT(BlueprintType)
struct VAMCHARACTERRUNTIME_API FVamGluteCorrectiveBasis
{
    GENERATED_BODY()
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Corrective") FName Morph;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Corrective") int32 Side=0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Corrective") int32 Target=0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Corrective") int32 Axis=0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Corrective") double MaximumCm=0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Corrective") TArray<double> RegionalRmsCm;
    // Sparse diagnostic samples, not a runtime CPU skinner.
    UPROPERTY() TArray<FVector> DebugPositions;
    UPROPERTY() TArray<FVector> DebugLocalDeltas;
};
USTRUCT(BlueprintType)
struct VAMCHARACTERRUNTIME_API FVamGluteCorrectiveDiagnostic
{
    GENERATED_BODY()
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Diagnostics") int32 Side=0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Diagnostics") int32 Target=0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Diagnostics") double SourceRms=0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Diagnostics") double ProceduralRms=0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Diagnostics") double SkinningResidualRms=0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Diagnostics") double RawP95=0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Diagnostics") double AdaptedP95=0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Diagnostics") double FinalP95=0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Diagnostics") double AttenuationRatio=0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Diagnostics") double SafetyLoss=0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Diagnostics") double SmoothingLoss=0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Diagnostics") int32 AffectedVertices=0;
    UPROPERTY() TArray<FVector> Positions;
    UPROPERTY() TArray<FVector> Raw;
    UPROPERTY() TArray<FVector> Adapted;
    UPROPERTY() TArray<FVector> Procedural;
    UPROPERTY() TArray<FVector> Final;
    UPROPERTY() TArray<FVector> SkinningResidual;
};
USTRUCT()
struct FVamGluteCorrectiveReplay
{
    GENERATED_BODY()
    UPROPERTY() int32 Side=0;
    UPROPERTY() int32 Target=0;
    UPROPERTY() TArray<FVector> PosedDelta;
};
/** A source knee operator sampled into native GPU morphs on the final skin operator. */
USTRUCT()
struct FVamKneeCorrective
{
    GENERATED_BODY()
    UPROPERTY() int32 Bone=INDEX_NONE;
    UPROPERTY() int32 Parent=INDEX_NONE;
    UPROPERTY() FQuat ReferenceRotation=FQuat::Identity;
    UPROPERTY() TArray<double> Angles;
    UPROPERTY() TArray<FName> Morphs;
};
/** Family policy plus immutable geometry calibration. No character-ID branches or dynamic state. */
UCLASS(BlueprintType)
class VAMCHARACTERRUNTIME_API UVamGluteCorrectiveProfile : public UDataAsset
{
    GENERATED_BODY()
public:
    // Preserve serialized-default compatibility: old assets omit these values.
    // Final-weight builders stamp schema 4; runtime requires a matching data seal.
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Corrective") int32 SchemaVersion=1;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Corrective") FString Algorithm=TEXT("glute-corrective-g06-v1");
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Corrective") FString SkeletonFamily;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Corrective") FString SourceTopologyIdentity;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Corrective") FString FamilyPolicyIdentity;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Corrective") FString Provenance;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Corrective") FVector MetricDegrees=FVector(60,35,30);
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Corrective") double BlendWidth=1.2;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Corrective") double MaximumDimensionFraction=.12;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Corrective") TArray<FVamGlutePoseTarget> Targets;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Corrective") TArray<FVector> BuildDimensions;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Corrective") TArray<FVamGluteCorrectiveBasis> Bases;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Corrective") int32 ReusedSourceVertexCount=0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Corrective|Diagnostics") FString FidelityAuditJson;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Corrective|Diagnostics") FString FamilyReferenceJson;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Corrective|Diagnostics") TArray<FVamGluteCorrectiveDiagnostic> Diagnostics;
    UPROPERTY(VisibleAnywhere, Category="Corrective|Identity") TMap<FString,FString> FinalDataDigests;
    UPROPERTY() TSoftObjectPtr<class UVamCharacterDefinition> OriginalInfluenceDefinition;
    UPROPERTY() FString SourceFamilyJson;
    UPROPERTY() TArray<FVamGluteCorrectiveReplay> AuthoredReplay;
    // Kept separate so legacy replay serialization and data seals stay valid.
    UPROPERTY() TArray<FVamGluteCorrectiveReplay> FixedAuthoredReplay;
    UPROPERTY() TArray<FVamKneeCorrective> Knees;
    UPROPERTY() FString KneeSourceDigest;
    UPROPERTY(VisibleAnywhere, Category="Corrective|Diagnostics") FString FinalReplayJson;
    TMap<FString,FString> ComputeFinalData(class UVamCharacterDefinition* Definition, class UVamGluteStructureProfile* Glute, class UVamLegJiggleProfile* Leg, class UVamBreastJiggleProfile* Breast, FString& Error) const;
    FString ValidateFinalData(class UVamCharacterDefinition* Definition, class UVamGluteStructureProfile* Glute, class UVamLegJiggleProfile* Leg, class UVamBreastJiggleProfile* Breast) const;
    UFUNCTION(BlueprintPure, Category="Corrective") bool IsValidProfile() const;
};
struct FVamGluteSide;
namespace VamGluteCorrective
{
    VAMCHARACTERRUNTIME_API TMap<FName,float> KneeWeights(const UVamGluteCorrectiveProfile&,const TArray<FTransform>& Pose,const TArray<FTransform>* ShapeReference=nullptr);
    VAMCHARACTERRUNTIME_API FVector CurvatureDelta(const FVamGluteSide& S,const FVector& Point,const FVector& Degrees,const FVamGluteFoldState& Fold);
    /** Cardinal normalized radial interpolation, positive weights, no matrix conditioning problem. */
    VAMCHARACTERRUNTIME_API TArray<double> Weights(const UVamGluteCorrectiveProfile& Profile,const FVamHipSidePose& Pose);
    VAMCHARACTERRUNTIME_API FVector ShapeScale(const FVector& Build,const FVector& Current);
}

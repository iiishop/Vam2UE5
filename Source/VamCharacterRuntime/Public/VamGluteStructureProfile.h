#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "VamGluteStructureProfile.generated.h"

/** Dimensionless engineering response coefficients. Generated from semantic role and G0 attachments. */
USTRUCT(BlueprintType)
struct VAMCHARACTERRUNTIME_API FVamGlutePoseResponse
{
    GENERATED_BODY()
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Refinement") FVector4 SupportGains=FVector4(0,0,0,0); // flex, extension, abduction, rotation
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Refinement") FVector FlexionOffset=FVector::ZeroVector;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Refinement") FVector ExtensionOffset=FVector::ZeroVector;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Refinement") FVector AbductionOffset=FVector::ZeroVector;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Refinement") FVector RotationOffset=FVector::ZeroVector;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Refinement") FVector OrientationGains=FVector::ZeroVector; // abduction, flexion, axial twist
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Refinement") FVector MaximumOffsetFraction=FVector(.1);
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Refinement") double MaximumDownwardFraction=.05;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Refinement") double ThighFollow=0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Refinement") double PelvisTether=0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Refinement") double ProjectionRetention=.8;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Refinement") double MaximumOrientationRadians=.3;
};
USTRUCT(BlueprintType)
struct VAMCHARACTERRUNTIME_API FVamGluteFoldSemanticMap
{
    GENERATED_BODY()
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Fold Semantics") FVector MedialInfraglutealAnchor=FVector::ZeroVector;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Fold Semantics") FVector MiddleTransition=FVector::ZeroVector;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Fold Semantics") FVector LateralFade=FVector::ZeroVector;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Fold Semantics") FVector ExtensionGains=FVector::ZeroVector;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Fold Semantics") FVector FlexionStretchGains=FVector::ZeroVector;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Fold Semantics") FVector AbductionGains=FVector::ZeroVector;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Fold Semantics") FVector RotationGains=FVector::ZeroVector;
};
/** G0 uses cm and pelvis-relative posterior/transverse/superior (right handed) coordinates.
 * Effective volumes and support are engineering proxies, not anatomical measurements. */
USTRUCT(BlueprintType)
struct VAMCHARACTERRUNTIME_API FVamGluteRegion
{
    GENERATED_BODY()
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Glute") FName Semantic;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Refinement") FVamGlutePoseResponse PoseResponse;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Glute") int32 BoneIndex=INDEX_NONE;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Glute") FVector Rest=FVector::ZeroVector;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Glute") FVector PelvisPoint=FVector::ZeroVector;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Glute") FVector ThighPointLocal=FVector::ZeroVector;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Glute") double PelvisAttachment=0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Glute") double ThighAttachment=0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Glute") double EffectiveVolumeCm3=0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Glute") double MassFractionCandidate=0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Glute") FVector MassCenter=FVector::ZeroVector;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Glute") FVector InertiaCandidate=FVector::ZeroVector;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Glute") double LeverArmCm=0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Glute") double SupportBaseline=0;
};
USTRUCT(BlueprintType)
struct VAMCHARACTERRUNTIME_API FVamGluteShapeResponse
{
    GENERATED_BODY()
    UPROPERTY() FName Parameter;
    UPROPERTY() double DefaultValue=0;
    UPROPERTY() double LogVolume=0;
    UPROPERTY() double SupportAreaDelta=0;
    UPROPERTY() FVector COM=FVector::ZeroVector;
    UPROPERTY() FVector Dimensions=FVector::ZeroVector;
    UPROPERTY() TArray<FVector> RestDeltas;
    UPROPERTY() TArray<FVector> PelvisDeltas;
    UPROPERTY() TArray<FVector> ThighDeltas;
    UPROPERTY() TArray<double> VolumeSlopes;
    UPROPERTY() TArray<double> PelvisAttachmentDeltas;
    UPROPERTY() TArray<FVector> MassCenterDeltas;
};
USTRUCT(BlueprintType)
struct VAMCHARACTERRUNTIME_API FVamGluteSide
{
    GENERATED_BODY()
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Glute") FName Side;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Hip Pose") FVector FemurAxisInAnchor=FVector(0,0,-1);
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Fold Semantics") FVamGluteFoldSemanticMap FoldSemanticMap;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Glute") int32 PelvisBone=INDEX_NONE;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Glute") int32 ThighBone=INDEX_NONE;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Glute") int32 SourceGluteBone=INDEX_NONE;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Glute") int32 AnchorBone=INDEX_NONE;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Glute") double SideSign=1;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Glute") FTransform AnchorLocal;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Glute") FTransform RestThighInAnchor;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Glute") double EffectiveVolumeCm3=0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Glute") double SupportAreaCm2=0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Glute") FVector COM=FVector::ZeroVector;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Glute") FVector Dimensions=FVector::ZeroVector;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Glute") TArray<FVamGluteRegion> Regions;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Glute") TArray<float> RegionWeights;
    UPROPERTY() TArray<FVector> RegionPoints;
    /** Reserved semantics only: medial infragluteal anchor, mid transition, lateral fade. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Glute") TArray<FVector> FoldReferences;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Glute") TArray<FVamGluteShapeResponse> ShapeResponses;
};
UCLASS(BlueprintType)
class VAMCHARACTERRUNTIME_API UVamGluteStructureProfile : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Glute") int32 SchemaVersion=1;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Refinement") int32 RefinementVersion=0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Hip Pose") FTransform RestPelvisComponent;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Glute") FString Algorithm=TEXT("glute-structure-g0-v1");
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Glute") FString SourceTopologyIdentity;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Glute") FString SkeletonFamily;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Glute") FString RegionProvenance;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Glute") FString SkinWeightIdentity;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Glute") double SkinTransferMaximum=.7;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Glute") double SkinTransferFullConfidence=1;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Glute") int32 SourceBoneCount=0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Glute") double DensityCandidateKgPerCm3=.00105;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Glute") double EffectiveModulusPa=5000;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Glute") double PassiveTensionGain=4;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Glute") double MaximumLogStretch=.45;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Glute") TArray<FVamGluteSide> Sides;
    bool IsValidProfile() const;
};

#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "VamGluteStructureProfile.generated.h"

/** G0 uses cm and pelvis-relative posterior/transverse/superior (right handed) coordinates.
 * Effective volumes and support are engineering proxies, not anatomical measurements. */
USTRUCT(BlueprintType)
struct VAMCHARACTERRUNTIME_API FVamGluteRegion
{
    GENERATED_BODY()
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Glute") FName Semantic;
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
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Glute") FString Algorithm=TEXT("glute-structure-g0-v1");
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Glute") FString SourceTopologyIdentity;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Glute") FString SkeletonFamily;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Glute") FString RegionProvenance;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Glute") FString SkinWeightIdentity;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Glute") int32 SourceBoneCount=0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Glute") double DensityCandidateKgPerCm3=.00105;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Glute") double EffectiveModulusPa=5000;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Glute") double PassiveTensionGain=4;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Glute") double MaximumLogStretch=.45;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Glute") TArray<FVamGluteSide> Sides;
    bool IsValidProfile() const;
};

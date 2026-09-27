#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "VamBreastJiggleProfile.generated.h"

/** Coordinates: X anterior, Y lateral (mirrored through side sign), Z superior. cm, seconds, kg. */
USTRUCT(BlueprintType)
struct VAMCHARACTERRUNTIME_API FVamBreastNodeParameters
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Breast") FName Semantic;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Breast") int32 BoneIndex=INDEX_NONE;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Breast") FVector Rest=FVector::ZeroVector;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Breast") double MassFraction=.2;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Calibration") double EffectiveVolumeCm3=0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Calibration") FVector MassCenter=FVector::ZeroVector;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Calibration") FVector SupportStiffness=FVector::ZeroVector;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Calibration") double LeverArmCm=0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Calibration") FVector NegativeSoftFraction=FVector(.45,.65,.65);
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Breast") FVector FrequencyHz=FVector(3,3,3);
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Breast") FVector DampingRatio=FVector(.3,.3,.3);
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Breast") FVector PositiveLimitCm=FVector(4,3,4);
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Breast") FVector NegativeLimitCm=FVector(2,3,3);
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Breast") FVector PositiveNonlinearity=FVector(1,1,1);
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Breast") FVector NegativeNonlinearity=FVector(3,1,1);
};
USTRUCT(BlueprintType)
struct VAMCHARACTERRUNTIME_API FVamBreastCoupling
{
    GENERATED_BODY()
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Calibration") int32 A=0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Calibration") int32 B=1;
    /** kg/s^2, with positions expressed in cm. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Calibration") FVector Stiffness=FVector::ZeroVector;
};
USTRUCT(BlueprintType)
struct VAMCHARACTERRUNTIME_API FVamBreastShapeResponse
{
    GENERATED_BODY()
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Breast") FName Parameter;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Breast") double DefaultValue=0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Breast") double LogVolumeSlope=0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Breast") FVector AnchorTranslationDelta=FVector::ZeroVector;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Breast") double DepthDelta=0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Breast") double SupportAreaDelta=0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Breast") FVector COMDelta=FVector::ZeroVector;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Breast") TArray<FVector> NodeDeltas;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Breast") double RadiusDelta=0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Calibration") FVector SizeDelta=FVector::ZeroVector;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Calibration") FVector RootSizeDelta=FVector::ZeroVector;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Calibration") TArray<double> NodeVolumeLogSlopes;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Calibration") TArray<FVector> NodeMassCenterDeltas;
};
USTRUCT(BlueprintType)
struct VAMCHARACTERRUNTIME_API FVamBreastSideProfile
{
    GENERATED_BODY()
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Breast") FName Side;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Breast") int32 ChestBone=INDEX_NONE;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Breast") int32 AnchorBone=INDEX_NONE;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Breast") FTransform AnchorLocal;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Breast") FVector COM=FVector::ZeroVector;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Breast") double EffectiveVolumeCm3=0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Breast") double EffectiveRadiusCm=0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Breast") double SupportAreaCm2=0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Breast") double EffectiveDepthCm=0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Calibration") FVector SizeCm=FVector::ZeroVector;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Calibration") FVector RootSizeCm=FVector::ZeroVector;
    /** Symmetric inertia tensor about mass COM; off diagonal order XY, XZ, YZ. kg cm^2. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Calibration") FVector InertiaDiagonal=FVector::ZeroVector;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Calibration") FVector InertiaOffDiagonal=FVector::ZeroVector;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Calibration") FVector RotationalStiffness=FVector::ZeroVector;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Calibration") FVector RotationalDampingRatio=FVector(.3);
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Calibration") FVector AngularLimitRadians=FVector(.2);
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Calibration") FVector COMSupport=FVector::ZeroVector;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Calibration") FVector COMDampingRatio=FVector::ZeroVector;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Calibration") FVector COMPositiveLimit=FVector::ZeroVector;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Calibration") FVector COMNegativeLimit=FVector::ZeroVector;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Calibration") TArray<FVamBreastCoupling> Couplings;
    /** Natural frequencies are authored at this imported reference mass; shape/density change inertia, not elastic coefficients. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Breast") double ReferenceMassKg=0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Breast") double MassKg=0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Breast") FVector ImportedGravityLocal=FVector::ZeroVector;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Breast") TArray<FVamBreastNodeParameters> Nodes;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Breast") TArray<float> RegionWeights;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Breast") TArray<FVector> RegionPoints;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Breast") TArray<FVamBreastShapeResponse> ShapeResponses;
};
UCLASS(BlueprintType)
class VAMCHARACTERRUNTIME_API UVamBreastJiggleProfile : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Breast") int32 SchemaVersion=1;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Breast") FString BuildAlgorithmVersion=TEXT("breast-jiggle-v1");
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Breast") FString SourceTopologyIdentity;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Breast") FString SkeletonFamily;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Breast") FString RegionProvenance;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Breast") FString SkinWeightIdentity;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Breast") int32 SourceBoneCount=0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Calibration") int32 CompressedDonorVertices=0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Calibration") int32 SaturatedVerticesWithHelpers=0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Calibration") int32 SaturatedVerticesWithoutEligibleDonors=0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Calibration") double MeanHelperWeight=0;
    /** Effective network modulus (Pa), calibrated proxy, not a measured tissue material. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Calibration") double EffectiveModulusPa=1200;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Breast") TArray<FVamBreastSideProfile> Sides;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Physics") double DensityKgPerCm3=.001;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Physics") double CouplingHz=1.5;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Physics") double SoftLimitFraction=.65;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Physics") double MaximumRotationRadians=.25;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Runtime") double FixedStep=1./120.;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Runtime") int32 MaxSubsteps=16;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Runtime") double TeleportDistanceCm=150;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Runtime") double TeleportAngleRadians=1.75;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Runtime") bool bPreserveDisplacementOnTeleport=false;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Runtime") double SleepSpeedCmS=.002;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Runtime") double SleepAccelerationCmS2=.02;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Runtime") double LargeShapeChangeRatio=.25;
    bool IsValidProfile() const;
};

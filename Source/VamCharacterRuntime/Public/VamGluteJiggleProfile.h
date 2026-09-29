#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "VamGluteStructure.h"
#include "VamGluteJiggleProfile.generated.h"

USTRUCT(BlueprintType)
struct VAMCHARACTERRUNTIME_API FVamGluteDynamicNode
{
    GENERATED_BODY()
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="G1") FName Semantic;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="G1") int32 BoneIndex=INDEX_NONE;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="G1") double MassKg=0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="G1") FVector Rest=FVector::ZeroVector;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="G1") FVector COM=FVector::ZeroVector;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="G1") FVector Inertia=FVector::ZeroVector;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="G1") FVector PelvisPoint=FVector::ZeroVector;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="G1") FVector ThighPointLocal=FVector::ZeroVector;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="G1") double PelvisAttachment=0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="G1") double ThighAttachment=0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="G1") FVector Support=FVector::ZeroVector;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="G1") FVector DampingRatio=FVector(.32);
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="G1") FVector PositiveTravel=FVector::ZeroVector;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="G1") FVector NegativeTravel=FVector::ZeroVector;
};
USTRUCT(BlueprintType)
struct VAMCHARACTERRUNTIME_API FVamGluteDynamicEdge
{
    GENERATED_BODY()
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="G1") int32 A=0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="G1") int32 B=0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="G1") FVector Stiffness=FVector::ZeroVector;
};
USTRUCT(BlueprintType)
struct VAMCHARACTERRUNTIME_API FVamGluteDynamicSide
{
    GENERATED_BODY()
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="G1") FName Side;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="G1") double MassKg=0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="G1") FVector COM=FVector::ZeroVector;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="G1") FVector Dimensions=FVector::ZeroVector;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="G1") TArray<FVamGluteDynamicNode> Nodes;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="G1") TArray<FVamGluteDynamicEdge> Couplings;
};
/** Immutable calibration; mutable particles and histories belong to the component. */
UCLASS(BlueprintType)
class VAMCHARACTERRUNTIME_API UVamGluteJiggleProfile : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="G1") int32 SchemaVersion=1;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="G1") FString Algorithm=TEXT("glute-dual-attachment-g1-v1");
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="G1") FString SourceTopologyIdentity;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="G1") FString SkeletonFamily;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="G1") TArray<FVamGluteDynamicSide> Sides;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="G1") double DensityKgPerCm3=.00105;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="G1") double DynamicModulusFraction=.08;
    /** Preserve legacy assets; builders opt into a single total damping ratio shared by both attachments. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="G1") bool bNormalizedAttachmentDamping=false;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="G1") double FixedStep=1./120.;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="G1") int32 MaxSubsteps=16;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="G1") double SoftLimitFraction=.65;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="G1") double NonlinearGain=2;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="G1") double LimitHardening=40;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="G1") double TeleportDistanceCm=150;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="G1") double TeleportAngleRadians=1.5;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="G1") double PoseDiscontinuityRadians=.9;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="G1") double LargeShapeChangeRatio=.25;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="G1") double SleepSpeedCmS=.005;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="G1") double SleepDisplacementCm=.0001;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="G1") FString GravityPolicy=TEXT("G0.5 is current gravity-loaded equilibrium; cancel static gravity by support preload. World inertia remains active.");
    UFUNCTION(BlueprintPure, Category="G1") bool IsValidProfile() const;
};
namespace VamGluteDynamics
{
    VAMCHARACTERRUNTIME_API FVamGluteDynamicSide Calibrate(const UVamGluteJiggleProfile&,const FVamGluteSide&,const FVamGluteStructuralState&);
}

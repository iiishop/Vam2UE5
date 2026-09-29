#pragma once
#include "VamGluteJiggleProfile.h"
#include "VamLegJiggleProfile.generated.h"

USTRUCT(BlueprintType)
struct VAMCHARACTERRUNTIME_API FVamLegShapeResponse
{
    GENERATED_BODY()
    UPROPERTY() FName Parameter;
    UPROPERTY() double DefaultValue=0;
    UPROPERTY() TArray<FVector> RestDeltas;
    UPROPERTY() double LogVolume=0;
};
USTRUCT(BlueprintType)
struct VAMCHARACTERRUNTIME_API FVamLegSegment
{
    GENERATED_BODY()
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Leg Jiggle") FName Name;
    UPROPERTY() bool bCalf=false;
    UPROPERTY() int32 Side=0;
    UPROPERTY() int32 Pelvis=INDEX_NONE;
    UPROPERTY() int32 Thigh=INDEX_NONE;
    UPROPERTY() int32 Shin=INDEX_NONE;
    UPROPERTY() int32 Foot=INDEX_NONE;
    UPROPERTY() int32 AnchorBone=INDEX_NONE;
    UPROPERTY() FTransform AnchorLocal;
    UPROPERTY() TArray<FTransform> JointRest; // thigh/pelvis, shin/thigh, foot/shin
    UPROPERTY() TArray<FQuat> ParentRestRotations;
    UPROPERTY() FVector BodyUp=FVector::ZAxisVector;
    UPROPERTY() FVector LateralAxis=FVector::YAxisVector;
    UPROPERTY() double Length=0;
    UPROPERTY() double Radius=0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Leg Jiggle") double EffectiveVolumeCm3=0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Leg Jiggle") FVamGluteDynamicSide Dynamics;
    UPROPERTY() TArray<FVector> LengthCoefficients; // hip/knee/ankle logarithmic strain per radian
    UPROPERTY() TArray<FVector2D> SideCoefficients; // abduction/axial rotation
    UPROPERTY() TArray<float> RegionWeights;
    UPROPERTY() TArray<FVector> RegionPoints;
    UPROPERTY() TArray<FVamLegShapeResponse> ShapeResponses;
};
UCLASS(BlueprintType)
class VAMCHARACTERRUNTIME_API UVamLegJiggleProfile : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Leg Jiggle") int32 SchemaVersion=1;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Leg Jiggle") FString Algorithm=TEXT("leg-pose-tension-t1-v3-connected");
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Leg Jiggle") FString SkeletonFamily;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Leg Jiggle") FString SourceTopologyIdentity;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Leg Jiggle") FString Provenance;
    UPROPERTY() int32 SourceBoneCount=0;
    UPROPERTY() TArray<FName> SourceBoneNames;
    UPROPERTY() TArray<FTransform> SourceLocalBind;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Leg Jiggle") double DensityKgPerCm3=.00105;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Leg Jiggle") double PassiveGain=6;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Leg Jiggle") double PassiveDampingGain=.3;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Leg Jiggle") double PassiveSlack=.02;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Leg Jiggle") TArray<FVamLegSegment> Segments;
    /** Shared numerical kernel settings only; its Glute-specific asset validator is not used. */
    UPROPERTY(Instanced) TObjectPtr<UVamGluteJiggleProfile> Integration;
    UFUNCTION(BlueprintPure, Category="VaM|Leg Jiggle") bool IsValidProfile() const;
};
namespace VamLegDynamics
{
    VAMCHARACTERRUNTIME_API FVector JointAngles(const FVamLegSegment&,const TArray<FTransform>& Pose,FVector2D& SideAngles);
    VAMCHARACTERRUNTIME_API FVamGluteDynamicSide Evaluate(const UVamLegJiggleProfile&,const FVamLegSegment&,const FVector& Angles,const FVector2D& SideAngles,TArray<double>& Tension);
}

#pragma once
#include "VamGluteStructureProfile.h"
#include "VamHipPoseState.h"
#include "VamGluteStructure.generated.h"

USTRUCT(BlueprintType)
struct VAMCHARACTERRUNTIME_API FVamGluteRegionState
{
    GENERATED_BODY()
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Glute State") FTransform Transform;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Glute State") FVector StructuralOffset=FVector::ZeroVector;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Glute State") FQuat OrientationAdjustment=FQuat::Identity;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Glute State") FVector RegionalStiffnessBaseline=FVector::ZeroVector;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Glute State") FVector ThighPoint=FVector::ZeroVector;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Glute State") double Tension=0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Glute State") double PelvisAttachment=0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Glute State") double ThighAttachment=0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Glute State") double Support=0;
};
USTRUCT(BlueprintType)
struct VAMCHARACTERRUNTIME_API FVamGluteStructuralState
{
    GENERATED_BODY()
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Glute State") FVamHipSidePose HipPose;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Glute State") FVamGluteFoldState FoldState;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Glute State") FVector FinalRestCOM=FVector::ZeroVector;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Glute State") FVector HipAnglesDegrees=FVector::ZeroVector; // flexion, abduction, external rotation
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Glute State") TArray<FVamGluteRegionState> Regions;
};
namespace VamGluteStructure
{
    VAMCHARACTERRUNTIME_API void RefinePose(const UVamGluteStructureProfile& P,const FVamGluteSide& S,FVamGluteStructuralState& State);
    VAMCHARACTERRUNTIME_API void CalibratePoseRefinement(FVamGluteSide& Side);
    VAMCHARACTERRUNTIME_API FVamHipSidePose HipSidePose(const FVamGluteSide& Side,const FTransform& FemurInAnchor);
    VAMCHARACTERRUNTIME_API FVamHipPoseState CaptureHipPose(const UVamGluteStructureProfile& P,const TArray<FVamGluteSide>& Rest,const FTransform& Pelvis,const FTransform& LeftFemur,const FTransform& RightFemur,int32 ShapeRevision);
    VAMCHARACTERRUNTIME_API FQuat FiberBasis(const FVamGluteSide& S,const FVamGluteRegion& R);
    /** Pure pose function: deliberately no clock, delta time, velocity or mutable solver. */
    VAMCHARACTERRUNTIME_API FVamGluteStructuralState Evaluate(const UVamGluteStructureProfile& P,const FVamGluteSide& S,const FTransform& ThighInAnchor);
    VAMCHARACTERRUNTIME_API void ApplyShape(FVamGluteSide& S,const TMap<FName,float>& Values);
}

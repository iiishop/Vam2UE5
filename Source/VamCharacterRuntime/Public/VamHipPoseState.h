#pragma once
#include "CoreMinimal.h"
#include "VamHipPoseState.generated.h"

/** Signed anatomical coordinates, in radians; decomposed swing plus femur-axis twist. */
USTRUCT(BlueprintType)
struct VAMCHARACTERRUNTIME_API FVamHipSidePose
{
    GENERATED_BODY()
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Hip Pose") FName Side;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Hip Pose") FTransform FemurInPelvis;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Hip Pose") FTransform FemurInAnchor;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Hip Pose") FQuat RelativeOrientation=FQuat::Identity;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Hip Pose") double FlexionExtension=0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Hip Pose") double AbductionAdduction=0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Hip Pose") double ExternalInternalRotation=0;
};
/** A serializable snapshot of PRIMARY source bones, captured before any helper writes.
 * Pelvis angles are relative to imported anatomical axes in component space, not locomotion heading.
 * Revision is provenance only and never participates in the pose function. */
USTRUCT(BlueprintType)
struct VAMCHARACTERRUNTIME_API FVamHipPoseState
{
    GENERATED_BODY()
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Hip Pose") int32 SchemaVersion=1;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Hip Pose") int32 ShapeRevision=0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Hip Pose") FTransform PelvisComponent;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Hip Pose") FTransform LeftFemurComponent;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Hip Pose") FTransform RightFemurComponent;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Hip Pose") double PelvisTilt=0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Hip Pose") double PelvisYaw=0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Hip Pose") double PelvisRoll=0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Hip Pose") TArray<FVamHipSidePose> Sides;
};

USTRUCT(BlueprintType)
struct VAMCHARACTERRUNTIME_API FVamGluteFoldState
{
    GENERATED_BODY()
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Fold Semantics") double MedialAnchorFactor=0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Fold Semantics") double MiddleTransitionFactor=0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Fold Semantics") double LateralFadeFactor=0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Fold Semantics") FVector StretchState=FVector::ZeroVector;
};

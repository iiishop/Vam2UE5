#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "VamPhysicsShapeProfile.generated.h"

USTRUCT()
struct FVamCollisionPointDelta
{
    GENERATED_BODY()
    UPROPERTY() int32 Point=INDEX_NONE;
    UPROPERTY() FVector Delta=FVector::ZeroVector;
};
USTRUCT()
struct FVamCollisionMorph
{
    GENERATED_BODY()
    UPROPERTY() FName Parameter;
    UPROPERTY() float Baseline=0;
    UPROPERTY() TArray<FVamCollisionPointDelta> Deltas;
};
USTRUCT()
struct FVamCollisionFit
{
    GENERATED_BODY()
    UPROPERTY() FName Bone;
    UPROPERTY() int32 BoneIndex=INDEX_NONE;
    UPROPERTY() TArray<int32> Points;
    UPROPERTY() FQuat Rotation=FQuat::Identity;
    UPROPERTY() FVector Center=FVector::ZeroVector;
    UPROPERTY() float Radius=0;
    UPROPERTY() float Length=0;
    UPROPERTY() FVector BoundsMin=FVector::ZeroVector;
    UPROPERTY() FVector BoundsMax=FVector::ZeroVector;
};

/** Cooked differential capsule fitting in bone-local space. This is a rigid
    collision proxy, not a tissue solver or an anatomical segmentation. */
UCLASS(BlueprintType)
class VAMCHARACTERRUNTIME_API UVamPhysicsShapeProfile : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="VaM") FString BindSignature;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="VaM") FString MorphSetLockDigest;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="VaM") TSoftObjectPtr<class UPhysicsAsset> Physics;
    UPROPERTY() TArray<FVector> BaselinePoints;
    UPROPERTY() TArray<FVamCollisionMorph> Morphs;
    UPROPERTY() TArray<FVamCollisionFit> Fits;
    // Declared acceptance domain, checked before any instance mutation.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VaM") float MinimumScale=.2f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VaM") float MaximumScale=5.f;
    bool Fit(const TMap<FName,float>& Values, const TArray<FTransform>& ReferenceCS,
        TArray<FVamCollisionFit>& Result, FString& Error) const;
};

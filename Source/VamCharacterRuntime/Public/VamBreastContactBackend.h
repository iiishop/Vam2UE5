#pragma once
#include "CoreMinimal.h"
#include "ChaosFlesh/FleshComponent.h"
#include "ChaosFlesh/ChaosDeformableCollisionsComponent.h"
#include "VamBreastContactBackend.generated.h"

/** Concrete transient identity for a collision source (UObject itself is abstract). */
UCLASS(Transient)
class VAMCHARACTERRUNTIME_API UVamBreastContactSource : public UObject
{
    GENERATED_BODY()
};

/** Final-pose input for a private Chaos solver. No surface mesh is created. */
UCLASS(Transient)
class VAMCHARACTERRUNTIME_API UVamBreastContactFlesh : public UFleshComponent
{
    GENERATED_BODY()
public:
    TArray<FTransform> InputPose;
    TArray<FTransform> ReferencePose;
    TArray<FVector> OutputPositions;
    virtual FDataMapValue NewDeformableData() override;
    virtual void UpdateFromSimulation(const FDataMapValue* Buffer) override;
};

USTRUCT(BlueprintType)
struct VAMCHARACTERRUNTIME_API FVamBreastPressSphere
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Contact") FVector WorldCenter=FVector::ZeroVector;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Contact",meta=(ClampMin="0.1")) float RadiusCm=3;
    /** Optional flat platen. Legacy sphere sources retain their original behavior. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Contact") bool bPlaten=false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Contact") FVector HalfExtentCm=FVector(1,4,4);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Contact") FQuat WorldRotation=FQuat::Identity;
};

/** Explicit contact sources. Stable object identities permit radius changes and removal. */
UCLASS(Transient)
class VAMCHARACTERRUNTIME_API UVamBreastContactCollisions : public UDeformableCollisionsComponent
{
    GENERATED_BODY()
public:
    TArray<FVamBreastPressSphere> Spheres;
    virtual FThreadingProxy* NewProxy() override;
    virtual FDataMapValue NewDeformableData() override;
private:
    TArray<FVamBreastPressSphere> Previous;
    UPROPERTY(Transient) TArray<TObjectPtr<UObject>> Keys;
    UPROPERTY(Transient) TArray<TObjectPtr<UObject>> RetiredKeys;
};

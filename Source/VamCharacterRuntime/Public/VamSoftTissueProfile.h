#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "VamPhysicsShapeProfile.h"
#include "VamSoftTissueProfile.generated.h"

UENUM(BlueprintType)
enum class EVamSoftTissueQuality : uint8 { Off, Balanced, High };
UENUM(BlueprintType)
enum class EVamSoftTissueState : uint8 { Disabled, Loading, Building, WarmingUp, Ready, Resetting, Error };

USTRUCT(BlueprintType)
struct FVamSoftTissueRegion
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VaM") FName Name;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VaM") FName Bone;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VaM") int32 Cells=4;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VaM") float MinimumSkinWeight=.05f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VaM") float FullSkinWeight=.5f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VaM") float SupportFraction=.25f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VaM") float DensityKgPerCm3=.001f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VaM") float Stiffness=100000.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VaM") float Damping=.1f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VaM") float Incompressibility=.45f;
};
USTRUCT()
struct FVamTissueSkinWeight
{
    GENERATED_BODY()
    UPROPERTY() TArray<int32> Bones;
    UPROPERTY() TArray<float> Weights;
};
USTRUCT()
struct FVamTissueSection
{
    GENERATED_BODY()
    UPROPERTY() int32 MaterialSlot=0;
    UPROPERTY() int32 FirstVertex=0;
    UPROPERTY() int32 NumVertices=0;
    UPROPERTY() TArray<int32> Triangles;
};
/** Cooked engineering proxies and unchanged LOD0 render topology. No map or test actor dependency. */
UCLASS(BlueprintType)
class VAMCHARACTERRUNTIME_API UVamSoftTissueProfile : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="VaM") FString BackendVersion=TEXT("ChaosFlesh-CPU-surface/1");
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="VaM") FString BindSignature;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="VaM") FString MorphSetLockDigest;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="VaM") TSoftObjectPtr<class USkeletalMesh> Body;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="VaM") TArray<FVamSoftTissueRegion> Regions;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VaM") bool bGravity=true;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VaM") int32 WarmupFrames=4;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VaM") float MaximumStepSeconds=1.f/30;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VaM") float CollisionQueryMarginCm=100;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VaM") float MinimumTetVolumeRatio=.05f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VaM") float MaximumTetVolumeRatio=20;
    UPROPERTY() TArray<FVector> Positions;
    UPROPERTY() TArray<FVector> Normals;
    UPROPERTY() TArray<FVector2D> UV0;
    UPROPERTY() TArray<FVector2D> UV1;
    UPROPERTY() TArray<FVector2D> UV2;
    UPROPERTY() TArray<FVector2D> UV3;
    UPROPERTY() TArray<FLinearColor> Colors;
    /** Cooked smoothing groups across UV/material seams; no runtime positional search. */
    UPROPERTY() TArray<int32> NormalParents;
    UPROPERTY() TArray<FVamTissueSkinWeight> Skin;
    UPROPERTY() TArray<FVamCollisionMorph> Morphs;
    UPROPERTY() TArray<FName> ExpressionMorphs;
    UPROPERTY() TArray<FVamTissueSection> Sections;
    UPROPERTY() TArray<FVector> RestParticles;
    UPROPERTY() TArray<FVamTissueSkinWeight> ParticleSkin;
    UPROPERTY() TArray<int32> ParticleRegions;
    UPROPERTY() TArray<int32> ShapeSourceVertices;
    UPROPERTY() TArray<bool> Supports;
    UPROPERTY() TArray<FIntVector4> Tetrahedra;
    UPROPERTY() TArray<FIntVector> BoundaryTriangles;
    UPROPERTY() TArray<FIntVector4> SurfaceParents;
    UPROPERTY() TArray<FVector4f> SurfaceWeights;
    UPROPERTY() TArray<float> SurfaceMask;
    UPROPERTY() TArray<int32> SurfaceRegions;
};

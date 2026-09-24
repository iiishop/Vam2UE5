#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "VamShapeData.generated.h"

USTRUCT(BlueprintType)
struct VAMCHARACTERRUNTIME_API FVamBoneCenterDelta
{
    GENERATED_BODY()
    UPROPERTY(Category="VaM", EditAnywhere, BlueprintReadOnly) int32 BoneIndex = INDEX_NONE;
    UPROPERTY(Category="VaM", EditAnywhere, BlueprintReadOnly) FVector LocalTranslation = FVector::ZeroVector;
};

/** Shape is parameter state, never a captured posed/simulated surface. */
USTRUCT(BlueprintType)
struct VAMCHARACTERRUNTIME_API FVamShapeState
{
    GENERATED_BODY()
    UPROPERTY(Category="VaM", EditAnywhere, BlueprintReadWrite) TMap<FName,float> Values;
    UPROPERTY(Category="VaM", VisibleAnywhere, BlueprintReadOnly) int32 Revision = 0;
};

USTRUCT(BlueprintType)
struct VAMCHARACTERRUNTIME_API FVamShapeChange
{
    GENERATED_BODY()
    UPROPERTY(Category="VaM", BlueprintReadOnly) int32 ShapeRevision = 0;
    UPROPERTY(Category="VaM", BlueprintReadOnly) bool bCommitted = false;
    UPROPERTY(Category="VaM", BlueprintReadOnly) TArray<FName> Parameters;
    UPROPERTY(Category="VaM", BlueprintReadOnly) TArray<FName> Regions;
    UPROPERTY(Category="VaM", BlueprintReadOnly) TArray<FName> Bones;
    UPROPERTY(Category="VaM", BlueprintReadOnly) TArray<FSoftObjectPath> Parts;
};

/** Explicitly separates authored shape, current pose, and absent/future simulation. */
USTRUCT(BlueprintType)
struct VAMCHARACTERRUNTIME_API FVamCharacterState
{
    GENERATED_BODY()
    UPROPERTY(Category="VaM", BlueprintReadOnly) FVamShapeState CommittedShape;
    UPROPERTY(Category="VaM", BlueprintReadOnly) FVamShapeState PreviewShape;
    UPROPERTY(Category="VaM", BlueprintReadOnly) TArray<FTransform> PoseComponentSpace;
    UPROPERTY(Category="VaM", BlueprintReadOnly) double AnimationPoseTimeSeconds = -1;
    UPROPERTY(Category="VaM", BlueprintReadOnly) double RigidPoseTimeSeconds = -1;
    UPROPERTY(Category="VaM", BlueprintReadOnly) double SurfaceTimeSeconds = -1;
    UPROPERTY(Category="VaM", BlueprintReadOnly) double CollisionProxyTimeSeconds = -1;
    UPROPERTY(Category="VaM", BlueprintReadOnly) bool bHasSimulation = false;
    UPROPERTY(Category="VaM", BlueprintReadOnly) int32 SimulationShapeRevision = INDEX_NONE;
    UPROPERTY(Category="VaM", BlueprintReadOnly) TArray<FVector> SimulatedSurface;
};

UCLASS(BlueprintType)
class VAMCHARACTERRUNTIME_API UVamAppearancePreset : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(Category="VaM", EditAnywhere, BlueprintReadOnly) FString SourceIdentity;
    UPROPERTY(Category="VaM", EditAnywhere, BlueprintReadOnly) TMap<FName,float> Parameters;
};

USTRUCT(BlueprintType)
struct VAMCHARACTERRUNTIME_API FVamSurfaceRegion
{
    GENERATED_BODY()
    UPROPERTY(Category="VaM", EditAnywhere, BlueprintReadOnly) FName Id;
    UPROPERTY(Category="VaM", EditAnywhere, BlueprintReadOnly) FString SourceEvidence;
    UPROPERTY(Category="VaM", EditAnywhere, BlueprintReadOnly) FName AnatomicalSemantic;
    UPROPERTY(Category="VaM", EditAnywhere, BlueprintReadOnly) TArray<int32> SourceVertices;
    UPROPERTY(Category="VaM", EditAnywhere, BlueprintReadOnly) TArray<int32> InputTriangles;
};

/** Versioned source-domain interface; unknown anatomy stays unassigned. */
UCLASS(BlueprintType)
class VAMCHARACTERRUNTIME_API UVamGeometryBinding : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(Category="VaM", EditAnywhere, BlueprintReadOnly) int32 SchemaVersion = 1;
    UPROPERTY(Category="VaM", EditAnywhere, BlueprintReadOnly) FString TopologyDigest;
    UPROPERTY(Category="VaM", EditAnywhere, BlueprintReadOnly) TArray<int32> RenderToInput;
    UPROPERTY(Category="VaM", EditAnywhere, BlueprintReadOnly) TArray<int32> InputToSource;
    UPROPERTY(Category="VaM", EditAnywhere, BlueprintReadOnly) TArray<int32> InputTriangles;
    UPROPERTY(Category="VaM", EditAnywhere, BlueprintReadOnly) TArray<FVamSurfaceRegion> Regions;
    // Lossless native strings, no external paths or runtime VAR decoder dependency.
    UPROPERTY(Category="VaM", EditAnywhere, BlueprintReadOnly) FString ClothGeometryDataJson;
    UPROPERTY(Category="VaM", EditAnywhere, BlueprintReadOnly) FString HairSourceDataJson;
};

UCLASS(BlueprintType)
class VAMCHARACTERRUNTIME_API UVamShapeDefinition : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(Category="VaM", EditAnywhere, BlueprintReadOnly) int32 SchemaVersion = 1;
    UPROPERTY(Category="VaM", EditAnywhere, BlueprintReadOnly) FString MorphSetLockDigest;
    UPROPERTY(Category="VaM", EditAnywhere, BlueprintReadOnly) TArray<FTransform> NeutralLocalBind;
    UPROPERTY(Category="VaM", EditAnywhere, BlueprintReadOnly) FString FormulaDiagnosticsJson;
    UPROPERTY(Category="VaM", EditAnywhere, BlueprintReadOnly) TSoftObjectPtr<UVamGeometryBinding> Geometry;
};

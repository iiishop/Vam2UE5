#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "VamCharacterDefinition.generated.h"

class USkeletalMesh;
class USkeleton;

USTRUCT(BlueprintType)
struct VAMCHARACTERRUNTIME_API FVamMorphParameter
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VaM") FName Target;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VaM") FString SourceId;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VaM") float DefaultValue = 0;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VaM") float Minimum = 0;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VaM") float Maximum = 1;
};

/** Native-only runtime contract. Source archives and editor services are not dependencies. */
UCLASS(BlueprintType)
class VAMCHARACTERRUNTIME_API UVamCharacterDefinition : public UPrimaryDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Character") TSoftObjectPtr<USkeletalMesh> Body;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Character") TSoftObjectPtr<USkeleton> Skeleton;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Character") TArray<TSoftObjectPtr<USkeletalMesh>> Parts;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Character") TArray<FVamMorphParameter> Parameters;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Source") FString SourceIdentity;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Source") FString SourceDigest;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Source") FString BindSignature;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Source") FString SurfaceStatus = TEXT("partial");
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Source") TArray<FString> Limitations;
    // Only verified X0 + linear deltas are currently accepted by the host.
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Source") FString ShapeConvention = TEXT("neutral_plus_parameters");
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Source") bool bBuildVerified = false;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Source") int32 SchemaVersion = 1;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Source") FString SkeletonExtensionVersion = TEXT("source-v1");
};

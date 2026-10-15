#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "VamHairAsset.generated.h"

/** One reusable VaM hair resource, before fitting to any character. */
UCLASS(BlueprintType)
class VAMCHARACTERRUNTIME_API UVamHairAsset : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="VaM|Hair") FString SourceIdentity;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="VaM|Hair") FString DisplayName;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="VaM|Hair") FString SourcePath;
    /** Authored guides, scalp/wrap, topology, materials and settings; never fitted body data. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="VaM|Hair") FString SourceDataJson;
};

USTRUCT(BlueprintType)
struct FVamHairstyleEntry
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VaM|Hair") TSoftObjectPtr<UVamHairAsset> Hair;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VaM|Hair") bool bEnabled=true;
    /** Overrides belong to this composition, not to the shared source resource. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VaM|Hair") FString AppearanceOverridesJson;
};

/** A composition of one or more independent hair assets. Contains no character mesh. */
UCLASS(BlueprintType)
class VAMCHARACTERRUNTIME_API UVamHairstyle : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="VaM|Hair") FString BuildIdentity;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VaM|Hair") TArray<FVamHairstyleEntry> Entries;
    FString GetContentSignature() const;
};

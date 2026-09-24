#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "VamMaterialProfile.generated.h"
class UMaterialInterface;

USTRUCT(BlueprintType)
struct VAMCHARACTERRUNTIME_API FVamPartMaterialSet
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TArray<TSoftObjectPtr<UMaterialInterface>> Materials;
};

/** Ordered material override contract for one CharacterDefinition. */
UCLASS(BlueprintType)
class VAMCHARACTERRUNTIME_API UVamMaterialProfile : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TArray<TSoftObjectPtr<UMaterialInterface>> BodyMaterials;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TArray<FVamPartMaterialSet> PartMaterials;
};

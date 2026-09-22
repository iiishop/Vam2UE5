#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "VamSourceMapping.generated.h"

/** Auditable editor provenance. Heavy source payload is stripped from cooked runtime data. */
UCLASS(BlueprintType)
class VAMCHARACTERRUNTIME_API UVamSourceMapping : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(VisibleAnywhere, Category="Source") FString SourceDigest;
    UPROPERTY(VisibleAnywhere, Category="Source") FString BindSignature;
#if WITH_EDITORONLY_DATA
    UPROPERTY(VisibleAnywhere, Category="Source") FString SourceIRJson;
    UPROPERTY(VisibleAnywhere, Category="Source") FString MaterialIRJson;
    UPROPERTY(VisibleAnywhere, Category="Source") FString NativeContractJson;
    UPROPERTY(VisibleAnywhere, Category="Source") TArray<int32> RenderToInputVertex;
    UPROPERTY(VisibleAnywhere, Category="Source") TArray<int32> InputToSourceVertex;
#endif
};

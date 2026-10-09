#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "VamLowerBodyContactBuilder.generated.h"

/** Builds a continuous, skeleton-supported lower-body tissue envelope per side. */
UCLASS()
class VAMRESOURCEBROWSER_API UVamLowerBodyContactBuilder : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable, Category="VaM|Editor")
    static FString Append(class UVamCharacterDefinition* Definition, class UVamGluteStructureProfile* Glute,
        class UVamLegJiggleProfile* Leg, class UVamBreastContactProfile* Profile);
};

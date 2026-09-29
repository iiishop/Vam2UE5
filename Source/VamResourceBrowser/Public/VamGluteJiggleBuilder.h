#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "VamGluteJiggleBuilder.generated.h"
UCLASS()
class VAMRESOURCEBROWSER_API UVamGluteJiggleBuilder : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable, Category="VaM|Editor") static FString BuildSurfaceGuard(class UVamCharacterDefinition* Definition,class UVamGluteJiggleProfile* Profile);
    UFUNCTION(BlueprintCallable, Category="VaM|Editor") static FString Build(class UVamGluteStructureProfile* Structure,class UVamGluteJiggleProfile* Profile);
};

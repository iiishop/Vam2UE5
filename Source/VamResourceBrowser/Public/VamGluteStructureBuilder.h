#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "VamGluteStructureBuilder.generated.h"
UCLASS()
class VAMRESOURCEBROWSER_API UVamGluteStructureBuilder : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable, Category="VaM|Editor")
    static class UVamCharacterDefinition* Build(const FString& Root,class UVamCharacterDefinition* Source,class UVamGluteStructureProfile* Profile,class UVamGluteCorrectiveProfile* Corrective,const FString& FamilyJson,FString& Error);
    UFUNCTION(BlueprintCallable, Category="VaM|Editor")
    static FString Validate(class UVamCharacterDefinition* Definition,class UVamGluteStructureProfile* Profile);
};

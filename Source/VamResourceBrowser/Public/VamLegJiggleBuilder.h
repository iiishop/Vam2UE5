#pragma once
#include "Kismet/BlueprintFunctionLibrary.h"
#include "VamLegJiggleBuilder.generated.h"
class UVamCharacterDefinition;
class UVamLegJiggleProfile;
class UVamGluteStructureProfile;
UCLASS()
class VAMRESOURCEBROWSER_API UVamLegJiggleBuilder : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable, Category="VaM|Editor") static UVamCharacterDefinition* Build(const FString& Root,UVamCharacterDefinition* Source,UVamLegJiggleProfile* Profile,UVamGluteStructureProfile* Glute,const FString& FamilyJson,FString& Error);
    UFUNCTION(BlueprintCallable, Category="VaM|Editor") static FString Validate(UVamCharacterDefinition* Definition,UVamLegJiggleProfile* Profile);
};

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
    static class UVamCharacterDefinition* BuildCorrective(const FString& Root,class UVamCharacterDefinition* FinalDefinition,class UVamCharacterDefinition* OriginalInfluenceDefinition,class UVamGluteStructureProfile* Glute,class UVamGluteCorrectiveProfile* Corrective,class UVamBreastContactProfile* Contact,const FString& FamilyJson,FString& Error);
    UFUNCTION(BlueprintCallable, Category="VaM|Editor")
    static FString SealCorrective(class UVamCharacterDefinition* Definition,class UVamGluteStructureProfile* Glute,class UVamLegJiggleProfile* Leg,class UVamBreastJiggleProfile* Breast,class UVamGluteCorrectiveProfile* Corrective);
    UFUNCTION(BlueprintCallable, Category="VaM|Editor")
    static FString ValidateCorrective(class UVamCharacterDefinition* Definition,class UVamGluteStructureProfile* Glute,class UVamLegJiggleProfile* Leg,class UVamBreastJiggleProfile* Breast,class UVamGluteCorrectiveProfile* Corrective);
    UFUNCTION(BlueprintCallable, Category="VaM|Editor")
    static FString Validate(class UVamCharacterDefinition* Definition,class UVamGluteStructureProfile* Profile);
    UFUNCTION(BlueprintCallable, Category="VaM|Editor")
    static FString SpatialAudit(class UVamCharacterDefinition* Definition,class UVamGluteStructureProfile* Profile);
    UFUNCTION(BlueprintCallable, Category="VaM|Editor")
    static FString SurfaceTransferAudit(class UVamCharacterDefinition* Definition,class UVamGluteStructureProfile* Glute,class UVamBreastJiggleProfile* Breast);
};

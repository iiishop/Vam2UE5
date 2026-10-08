#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "VamBreastContactBuilder.generated.h"

UCLASS()
class VAMRESOURCEBROWSER_API UVamBreastContactBuilder : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    /** Editor-only remeshing with attachment, Morph and render-binding transfer. */
    static FString RemeshFTetWild(class UVamBreastContactProfile* Profile);
    UFUNCTION(BlueprintCallable, Category="VaM|Editor")
    static FString Build(class UVamCharacterDefinition* Definition,class UVamBreastJiggleProfile* Breast,class UVamBreastContactProfile* Profile,const FString& FamilyJson);
    UFUNCTION(BlueprintCallable, Category="VaM|Editor")
    static FString BuildGPUDeformer(class UVamBreastContactProfile* Profile,const FString& AssetPath);
    UFUNCTION(BlueprintCallable, Category="VaM|Editor")
    static FString BuildDeformer(class UVamBreastContactProfile* Profile,const FString& AssetPath);
    UFUNCTION(BlueprintCallable, Category="VaM|Editor")
    static class UVamCharacterDefinition* RefineRenderSurface(const FString& Root, class UVamCharacterDefinition* Definition, class UVamBreastJiggleProfile* Breast, class UVamBreastContactProfile* Profile, class UVamGluteStructureProfile* Glute, class UVamLegJiggleProfile* Leg, FString& Error);
    UFUNCTION(BlueprintCallable, Category="VaM|Editor")
    static FString InspectDeformer(class UVamBreastContactProfile* Profile);
};

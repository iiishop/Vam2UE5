#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "VamHairBuilder.generated.h"
UCLASS()
class VAMRESOURCEBROWSER_API UVamHairBuilder : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable,Category="VaM|Editor|Hair")
    static class UVamHairProfile* BuildHair(const FString& InputFile,const FString& AssetRoot,class USkeletalMesh* Body,const FString& GenerationIdentity,FString& Error,class UPhysicsAsset* Physics=nullptr);
    UFUNCTION(BlueprintCallable,Category="VaM|Editor|Hair")
    static class UVamHairstyle* BuildHairstyle(const FString& CatalogFile,const FString& LibraryRoot,FString& Error);
    UFUNCTION(BlueprintCallable,Category="VaM|Editor|Hair")
    static bool BindHairstyle(class UVamHairProfile* Cache,class UVamRuntimeConfiguration* Configuration,class UVamHairstyle* Hairstyle);
};

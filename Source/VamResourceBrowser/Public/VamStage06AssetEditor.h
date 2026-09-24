#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "VamStage06AssetEditor.generated.h"
class UPhysicsAsset;
class UVamRigProfile;
class USkeletalMesh;

UCLASS()
class VAMRESOURCEBROWSER_API UVamStage06AssetEditor : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable, Category="VaM|Stage06|Editor")
    static FString ConfigurePhysicsAsset(UPhysicsAsset* Asset, const UVamRigProfile* Rig);
    UFUNCTION(BlueprintCallable, Category="VaM|Stage06|Editor")
    static UPhysicsAsset* BuildPhysicsAsset(const FString& AssetPath, USkeletalMesh* Mesh, float MinimumBoneSizeCm, FString& Error);
};

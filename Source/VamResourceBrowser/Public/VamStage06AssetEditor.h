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
    UFUNCTION(BlueprintCallable, Category="VaM|RuntimeBuild|Editor")
    static bool PublishRuntimeConfiguration(class UVamRuntimeConfiguration* Configuration);
    UFUNCTION(BlueprintCallable, Category="VaM|RuntimeBuild|Editor")
    static bool BuildPhysicsShapeProfile(class UVamPhysicsShapeProfile* Profile, class UVamCharacterDefinition* Definition, UPhysicsAsset* Physics, FString& Error);
    UFUNCTION(BlueprintCallable, Category="VaM|RuntimeBuild|Editor")
    static bool SetRuntimeConfigurationIdentity(class UVamRuntimeConfiguration* Configuration, class UVamCharacterDefinition* Definition, const FString& Identity, const FString& ReceiptJson);
    UFUNCTION(BlueprintCallable, Category="VaM|RuntimeBuild|Editor")
    static FString DescribeMeshBinding(USkeletalMesh* Mesh);
    UFUNCTION(BlueprintCallable, Category="VaM|RuntimeBuild|Editor")
    static FString ConfigureRuntimePhysicsAsset(UPhysicsAsset* Asset, USkeletalMesh* Mesh, const UVamRigProfile* Rig);
    UFUNCTION(BlueprintCallable, Category="VaM|Stage06|Editor")
    static FString ConfigurePhysicsAsset(UPhysicsAsset* Asset, const UVamRigProfile* Rig);
    UFUNCTION(BlueprintCallable, Category="VaM|Stage06|Editor")
    static UPhysicsAsset* BuildPhysicsAsset(const FString& AssetPath, USkeletalMesh* Mesh, float MinimumBoneSizeCm, FString& Error);
};

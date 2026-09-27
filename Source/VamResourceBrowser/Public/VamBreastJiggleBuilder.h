#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "VamBreastJiggleBuilder.generated.h"
UCLASS()
class VAMRESOURCEBROWSER_API UVamBreastJiggleBuilder : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable, Category="VaM|Editor")
    static class UVamCharacterDefinition* Build(const FString& Root,class UVamCharacterDefinition* Source,
        class UVamBreastJiggleProfile* Profile,const FString& FamilyJson,FString& Error);
    UFUNCTION(BlueprintCallable, Category="VaM|Editor")
    static class UAnimSequence* CopyAnimation(const FString& Path,class UAnimSequence* Source,class USkeleton* Skeleton);
    UFUNCTION(BlueprintCallable, Category="VaM|Editor")
    static bool ExcludeRigidHelpers(class UPhysicsAsset* Physics,class USkeletalMesh* Mesh,class UVamBreastJiggleProfile* Profile);
    UFUNCTION(BlueprintCallable, Category="VaM|Editor")
    static FString Validate(class UVamCharacterDefinition* Definition,class UVamBreastJiggleProfile* Profile);
};

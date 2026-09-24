#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "VamRuntimeConfiguration.generated.h"

/** Immutable derived runtime bundle. The editor builder publishes it only after
    a separate process reloads and checks its persisted receipt. */
UCLASS(BlueprintType)
class VAMCHARACTERRUNTIME_API UVamRuntimeConfiguration : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="VaM") int32 SchemaVersion=2;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="VaM") bool bIndependentReloadVerified=false;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="VaM") FString BuildIdentity;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="VaM") FString SourceDigest;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="VaM") FString BindSignature;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="VaM") FString MorphSetLockDigest;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VaM") TSoftObjectPtr<class UVamCharacterDefinition> Definition;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VaM") TSoftObjectPtr<class UVamRigProfile> Rig;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VaM") TSoftClassPtr<class UVamShapeAnimInstance> AnimationClass;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VaM") TSoftObjectPtr<class UAnimSequence> BaseAnimation;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VaM") TSoftObjectPtr<class UPhysicsAsset> Physics;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VaM") TSoftObjectPtr<class UVamPhysicsShapeProfile> PhysicsShape;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VaM") TSoftObjectPtr<class UVamMaterialProfile> Materials;
#if WITH_EDITORONLY_DATA
    UPROPERTY(VisibleAnywhere, Category="Build") FString ReceiptJson;
#endif
};

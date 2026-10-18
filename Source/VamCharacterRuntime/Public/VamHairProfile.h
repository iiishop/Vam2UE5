#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "VamHairProfile.generated.h"

UENUM(BlueprintType)
enum class EVamHairQuality : uint8 { Coarse UMETA(Hidden), Fine };

/** Character-fit render/physics cache. The reusable resources live in UVamHairAsset. */
UCLASS(BlueprintType)
class VAMCHARACTERRUNTIME_API UVamHairProfile : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="VaM|Hair") FString BuildIdentity;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="VaM|Hair") FString SourceReceiptJson;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="VaM|Hair") FString BuildReportJson;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="VaM|Hair") TSoftObjectPtr<class UVamHairstyle> Hairstyle;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="VaM|Hair") FString HairstyleSignature;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="VaM|Hair") TSoftObjectPtr<class USkeletalMesh> ReferenceBody;
    /** Body collision proxies retain body volume; head surface has a body-specific fit. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="VaM|Hair") TSoftObjectPtr<class UPhysicsAsset> HairCollision;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VaM|Hair") FName AttachmentBone=TEXT("head");
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="VaM|Hair") TSoftObjectPtr<class UGroomAsset> FineGroom;
    /** Legacy serialized field only. New builds and runtime use FineGroom. */
    UPROPERTY() TSoftObjectPtr<class UGroomAsset> CoarseGroom;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="VaM|Hair") TArray<TSoftObjectPtr<class UMaterialInterface>> Materials;
};

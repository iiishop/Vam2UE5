#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "VamFleshCapabilityAsset.generated.h"

/** Backend experiment, not an accepted anatomical profile. Binds actual native
    render vertices to a closed engineering cage; never changes the source mesh. */
UCLASS(BlueprintType)
class VAMCHARACTERRUNTIME_API UVamFleshCapabilityAsset : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="VaM|Capability") TObjectPtr<class UFleshAsset> Flesh;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="VaM|Capability") TObjectPtr<class USkeletalMesh> Body;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VaM|Capability") TArray<TObjectPtr<class UMaterialInterface>> SurfaceMaterials;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="VaM|Capability") FName SourceBone;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="VaM|Capability") FString BindSignature;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="VaM|Capability") FString Limitation=TEXT("Backend capability experiment; no production rest calibration or shape transaction support");
    UPROPERTY() TArray<FVector> RestVertices;
    UPROPERTY() TArray<FIntVector4> Tetrahedra;
    UPROPERTY() TArray<FIntVector4> SurfaceParents;
    UPROPERTY() TArray<FVector4f> SurfaceWeights;
    UPROPERTY() TArray<float> SurfaceMask;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="VaM|Capability") float DensityKgPerCm3=.001f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="VaM|Capability") float RestVolumeCm3=0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="VaM|Capability") int32 BoundRenderVertices=0;
};

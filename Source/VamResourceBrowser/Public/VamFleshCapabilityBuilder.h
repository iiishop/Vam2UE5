#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "VamFleshCapabilityBuilder.generated.h"

UCLASS()
class VAMRESOURCEBROWSER_API UVamFleshCapabilityBuilder : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable, Category="VaM|Stage07")
    static FString Build(class UVamFleshCapabilityAsset* Target, class UVamCharacterDefinition* Definition, FName SourceBone, int32 Cells=4);
    UFUNCTION(BlueprintCallable, Category="VaM|Stage07")
    static FString DescribeSurfaceGraph(class UMeshDeformer* Deformer);
};

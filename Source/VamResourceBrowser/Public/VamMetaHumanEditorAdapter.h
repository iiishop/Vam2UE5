#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "VamMetaHumanEditorAdapter.generated.h"
class UStaticMesh;
class UBlueprint;

/** Project adapter, not an Epic API. Only linked into the Editor module. */
UCLASS()
class VAMRESOURCEBROWSER_API UVamMetaHumanEditorAdapter : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    /** Read-only LOD0 polygon groups for separating skin from eye auxiliaries. */
    UFUNCTION(BlueprintCallable, Category="VaM|MetaHuman|Editor") static FString InspectMeshSections(UObject* Mesh);
    UFUNCTION(BlueprintCallable, Category="VaM|MetaHuman|Editor") static FString Probe();
    UFUNCTION(BlueprintCallable, Category="VaM|MetaHuman|Editor")
    static UStaticMesh* CreateTarget(const FString& AssetPath, const TArray<FVector>& Vertices, const TArray<int32>& Triangles, FString& Error);
    UFUNCTION(BlueprintCallable, Category="VaM|MetaHuman|Editor")
    static bool Conform(UObject* Character, UStaticMesh* Target, const TMap<int32,FVector>& Keypoints, const FString& CalibrationJson, FString& Error);
    UFUNCTION(BlueprintCallable, Category="VaM|MetaHuman|Editor") static FString ExportCalibration(UObject* Character, UStaticMesh* Target);
    /** Read-only geometry evidence in the solved input pose or official A pose. */
    UFUNCTION(BlueprintCallable, Category="VaM|MetaHuman|Editor") static FString InspectFitGeometry(UObject* Character, UStaticMesh* Target, bool Posed);
    UFUNCTION(BlueprintCallable, Category="VaM|MetaHuman|Editor") static bool RefineFit(UObject* Character, UStaticMesh* Target, const FString& ParamsJson, FString& Error);
    UFUNCTION(BlueprintCallable, Category="VaM|MetaHuman|Editor") static bool HasFullRig(UObject* Character);
    UFUNCTION(BlueprintCallable, Category="VaM|MetaHuman|Editor") static bool IsCharacterSourceValid(UObject* Character);
    UFUNCTION(BlueprintCallable, Category="VaM|MetaHuman|Editor") static FString InspectAssembly(AActor* Actor);
    UFUNCTION(BlueprintCallable, Category="VaM|MetaHuman|Editor") static bool AttachRuntime(UBlueprint* Blueprint, FString& Error);
};

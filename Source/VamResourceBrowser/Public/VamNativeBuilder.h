#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "VamCharacterDefinition.h"
#include "VamNativeBuilder.generated.h"
class USkeletalMesh;
class UMaterialInterface;
class UVamSourceMapping;

USTRUCT(BlueprintType)
struct FVamBuildBone
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VaM") FName Name;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VaM") int32 Parent = -1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VaM") FTransform LocalBind;
};
USTRUCT(BlueprintType)
struct FVamBuildInfluence
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VaM") int32 Vertex = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VaM") int32 Bone = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VaM") float Weight = 0;
};
USTRUCT(BlueprintType)
struct FVamBuildMorph
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VaM") FName Name;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VaM") TArray<FVector> Deltas;
};
USTRUCT(BlueprintType)
struct FVamNativeMeshInput
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VaM") TArray<FVector> Vertices;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VaM") TArray<FVector> Normals;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VaM") TArray<FVector2D> UV;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VaM") TArray<int32> Triangles;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VaM") TArray<int32> TriangleMaterials;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VaM") TArray<TObjectPtr<UMaterialInterface>> Materials;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VaM") TArray<int32> SourceVertices;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VaM") TArray<FVamBuildBone> Bones;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VaM") TArray<FVamBuildInfluence> Influences;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VaM") TArray<FVamBuildMorph> Morphs;
};

/** Low-level editor builder; caller must establish source semantics before calling. Never overwrites. */
UCLASS()
class VAMRESOURCEBROWSER_API UVamNativeBuilder : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable, Category="VaM|Editor")
    static USkeletalMesh* BuildMesh(const FString& AssetPath, const FVamNativeMeshInput& Input, FString& Error);
    UFUNCTION(BlueprintCallable, Category="VaM|Editor")
    static TArray<int32> GetRenderToInputMap(USkeletalMesh* Mesh);
    UFUNCTION(BlueprintCallable, Category="VaM|Editor")
    static UVamCharacterDefinition* CreateDefinition(const FString& AssetPath, USkeletalMesh* Body,
        const TArray<FVamMorphParameter>& Parameters, const FString& SourceIdentity, const FString& SourceDigest, const FString& BindSignature);
    UFUNCTION(BlueprintCallable, Category="VaM|Editor")
    static bool ShareCompatibleSkeleton(USkeletalMesh* Part, USkeletalMesh* Body);
    UFUNCTION(BlueprintCallable, Category="VaM|Editor")
    static void SetBuildLimitations(UVamCharacterDefinition* Definition, const TArray<FString>& Limitations);
    UFUNCTION(BlueprintCallable, Category="VaM|Editor")
    static void SetAppearanceBaseline(UVamCharacterDefinition* Definition);
    UFUNCTION(BlueprintCallable, Category="VaM|Editor")
    static UVamSourceMapping* CreateSourceMapping(const FString& AssetPath, const FString& SourceDigest,
        const FString& BindSignature, const FString& SourceIR, const FString& MaterialIR,
        const FString& Contract, const TArray<int32>& RenderToInput, const TArray<int32>& InputToSource);
};

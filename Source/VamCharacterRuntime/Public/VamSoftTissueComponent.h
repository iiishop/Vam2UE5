#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "VamSoftTissueProfile.h"
#include "VamShapeData.h"
#include "VamSoftTissueComponent.generated.h"

USTRUCT(BlueprintType)
struct FVamBodySurfaceOutput
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly, Category="VaM") bool Valid=false;
    UPROPERTY(BlueprintReadOnly, Category="VaM") int32 SchemaVersion=1;
    UPROPERTY(BlueprintReadOnly, Category="VaM") FName Producer=TEXT("ChaosFleshCPUSurface");
    UPROPERTY(BlueprintReadOnly, Category="VaM") FGuid Instance;
    UPROPERTY(BlueprintReadOnly, Category="VaM") FName Space=TEXT("CharacterComponentCm");
    UPROPERTY(BlueprintReadOnly, Category="VaM") FTransform ToWorld;
    UPROPERTY(BlueprintReadOnly, Category="VaM") int64 CharacterGeneration=0;
    UPROPERTY(BlueprintReadOnly, Category="VaM") int32 ShapeRevision=INDEX_NONE;
    UPROPERTY(BlueprintReadOnly, Category="VaM") int32 TeleportRevision=0;
    UPROPERTY(BlueprintReadOnly, Category="VaM") int32 PoseRevision=INDEX_NONE;
    UPROPERTY(BlueprintReadOnly, Category="VaM") int64 SolverRevision=0;
    UPROPERTY(BlueprintReadOnly, Category="VaM") double SolverTimeSeconds=0;
    UPROPERTY(BlueprintReadOnly, Category="VaM") double PublishedWorldTimeSeconds=-1;
    UPROPERTY(BlueprintReadOnly, Category="VaM") FName ContactRepresentation=TEXT("ClosedEngineeringCageTriangles");
    UPROPERTY(BlueprintReadOnly, Category="VaM") TArray<FVector> CollisionVertices;
    UPROPERTY(BlueprintReadOnly, Category="VaM") TArray<FIntVector> CollisionTriangles;
    UPROPERTY(BlueprintReadOnly, Category="VaM") TObjectPtr<class UProceduralMeshComponent> SurfaceResource;
};

/** Standard, character-owned capability. No level authoring or editor service is required. */
UCLASS(ClassGroup=(VaM), meta=(BlueprintSpawnableComponent))
class VAMCHARACTERRUNTIME_API UVamSoftTissueComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UVamSoftTissueComponent();
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="VaM|SoftTissue") EVamSoftTissueState State=EVamSoftTissueState::Disabled;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="VaM|SoftTissue") FString LastError;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="VaM|SoftTissue") int32 UnsupportedColliders=0;
    UFUNCTION(BlueprintCallable, Category="VaM|SoftTissue") void SetSoftTissueEnabled(bool Enabled);
    UFUNCTION(BlueprintCallable, Category="VaM|SoftTissue") void SetSoftTissueQuality(EVamSoftTissueQuality Quality);
    UFUNCTION(BlueprintCallable, Category="VaM|SoftTissue") void ResetSoftTissue();
    UFUNCTION(BlueprintPure, Category="VaM|SoftTissue") FVamBodySurfaceOutput GetBodySurfaceOutput() const;
    UFUNCTION(BlueprintPure, Category="VaM|SoftTissue") EVamSoftTissueQuality GetSoftTissueQuality() const { return Quality; }
    /** Explicit interaction objects supplement ordinary WorldStatic/WorldDynamic overlaps. */
    UFUNCTION(BlueprintCallable, Category="VaM|SoftTissue") void RegisterInteractionCollider(class UPrimitiveComponent* Collider);
    UFUNCTION(BlueprintCallable, Category="VaM|SoftTissue") void UnregisterInteractionCollider(class UPrimitiveComponent* Collider);
    void CharacterUnloading();
protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* TickFunction) override;
private:
    UFUNCTION() void ShapeChanged(const FVamShapeChange& Change);
    void LoadProfile();
    bool BuildRuntime();
    void ReleaseRuntime();
    void Fail(const FString& Message);
    void UpdateSurface();
    void UpdateCollisions();
    UPROPERTY(Transient) TObjectPtr<class UVamCharacterComponent> Character;
    UPROPERTY(Transient) TObjectPtr<UVamSoftTissueProfile> Profile;
    UPROPERTY(Transient) TObjectPtr<class UVamTissueFleshComponent> Flesh;
    UPROPERTY(Transient) TObjectPtr<class UDeformableSolverComponent> Solver;
    UPROPERTY(Transient) TObjectPtr<class UVamTissueCollisionComponent> Collisions;
    UPROPERTY(Transient) TObjectPtr<class UProceduralMeshComponent> Surface;
    UPROPERTY(Transient) TObjectPtr<class UFleshAsset> InstanceRest;
    UPROPERTY(Transient) FVamBodySurfaceOutput Output;
    TSharedPtr<struct FStreamableHandle> Pending;
    TArray<TWeakObjectPtr<class UPrimitiveComponent>> ExplicitColliders;
    TArray<FVector> ShapedParticles,SkinnedParticles,Vertices;
    TArray<FTransform> ReferenceCS;
    EVamSoftTissueQuality Quality=EVamSoftTissueQuality::Off;
    EVamSoftTissueQuality LastEnabledQuality=EVamSoftTissueQuality::Balanced;
    bool QualityOverride=false,OriginalVisible=true;
    uint64 Generation=MAX_uint64,LoadTicket=0;
    int32 ShapeRevision=INDEX_NONE,TeleportRevision=0,Warmup=0;
    double SolverTime=0;
};

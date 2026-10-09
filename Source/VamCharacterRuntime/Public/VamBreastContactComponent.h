#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "VamBreastContactProfile.h"
#include "VamBreastContactBackend.h"
#include "VamGPUContact.h"
#include "VamBreastContactComponent.generated.h"

class UPrimitiveComponent;

/** Per-character contact solver. Final pose is sampled after native bone publication. */
UCLASS(ClassGroup=(VaM),meta=(BlueprintSpawnableComponent))
class VAMCHARACTERRUNTIME_API UVamBreastContactComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UVamBreastContactComponent();
    /** Prefer resident GPU contact when the profile supports it; unsupported geometry falls back to CPU. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VaM|Breast Contact") bool bUseGPU=true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter=SetContactEnabled, Category="VaM|Breast Contact") bool bEnabled=true;
    /** Disabling immediately releases contact simulation; Jiggle is unaffected. */
    UFUNCTION(BlueprintSetter, Category="VaM|Breast Contact") void SetContactEnabled(bool bNewEnabled);
    UFUNCTION(BlueprintPure, Category="VaM|Breast Contact") bool IsContactEnabled() const { return bEnabled; }
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VaM|Breast Contact") bool bShowCage=false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VaM|Breast Contact") bool bShowContacts=false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VaM|Breast Contact") bool bWorldCollision=true;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="VaM|Breast Contact") bool bSoftCollision=true;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="VaM|Breast Contact") bool bForceFeedback=true;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="VaM|Breast Contact") FVector ContactForceNewtons=FVector::ZeroVector;
    /** Per-instance material override: 0 uniform tissue, 1 profile nipple preservation. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VaM|Breast Contact",meta=(ClampMin="0",ClampMax="1")) float NippleShapePreservationScale=1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VaM|Breast Contact") TArray<FVamBreastPressSphere> PressSpheres;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="VaM|Breast Contact") FString Status=TEXT("Profile absent");
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="VaM|Breast Contact") TArray<FVamBreastContactVolumeState> VolumeState;
    UFUNCTION(BlueprintCallable, Category="VaM|Breast Contact") void ResetContact();
    UFUNCTION(BlueprintCallable, Category="VaM|Breast Contact") void SetDebugPress(int32 Side,float DepthFraction);
    /** Body-centered sphere by default; offsets are in anatomical ML/SI radius units. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VaM|Breast Contact") bool bDebugPlaten=false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VaM|Breast Contact") FVector2D DebugPressOffset=FVector2D::ZeroVector;
    UFUNCTION(BlueprintPure, Category="VaM|Breast Contact") FString Diagnostics() const;
    virtual void TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* TickFunction) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    int32 GetActiveSolverCount() const { return GPUHandle.IsValid()?1:Solvers.Num(); }
    uint64 GetCageSnapshot(TArray<FVector>& Rest,TArray<FVector>& Current) const { return VamGPUReadDiagnostic(GPUHandle,Rest,Current); }
    bool SuppliesContactForceFor(UPrimitiveComponent* Component) const;
    double GetMaxContactResidualCm() const { return MaxContactResidualCm; }
    FVector2D GetBoundSurfaceResidualCm() const { return BoundSurfaceResidualCm; }
private:
    FVamGPUContactHandlePtr GPUHandle;
    TArray<float> GPUInverseMass;
    bool bGPUActive=false;
    bool bGPURejected=false;
    uint64 GPUDiagnosticFrame=0,GPULoadFrame=0;
    TMap<uint64,TWeakObjectPtr<class UPrimitiveComponent>> ReactionTargets;
    TMap<uint64,FName> ReactionBones;
    bool Initialize();
    void Release();
    void AddVolumeConstraint(class UDeformableSolverComponent* Solver,UVamBreastContactFlesh* Flesh);
    UPROPERTY(Transient) TObjectPtr<class UVamCharacterComponent> Character;
    UPROPERTY(Transient) TObjectPtr<class USkeletalMeshComponent> Body;
    UPROPERTY(Transient) TObjectPtr<UVamBreastContactProfile> Profile;
    UPROPERTY(Transient) TArray<TObjectPtr<class UDeformableSolverComponent>> Solvers;
    UPROPERTY(Transient) TArray<TObjectPtr<UVamBreastContactFlesh>> Flesh;
    UPROPERTY(Transient) TObjectPtr<UVamBreastContactCollisions> Collisions;
    UPROPERTY(Transient) TObjectPtr<UDeformableCollisionsComponent> WorldCollisions;
    TArray<TWeakObjectPtr<class UPrimitiveComponent>> WorldSources;
    UPROPERTY(Transient) TObjectPtr<UFleshComponent> SurfaceProducer;
    UPROPERTY(Transient) TArray<TObjectPtr<class UFleshAsset>> InstanceAssets;
    TArray<FVector> ShapedRest;
    TArray<FTransform> ReferenceCS;
    UPROPERTY(Transient) TObjectPtr<UMeshDeformer> PreviousDeformer;
    bool bPreviousDeformerOverride=false;
    double MaxContactResidualCm=0;
    FVector2D BoundSurfaceResidualCm=FVector2D::ZeroVector;
    int32 ActivePressSphereCount=0;
    double TickMs=0,SolveMs=0,PublishMs=0,InitMs=0;
    double NativeMaterialMs=0,VolumeConstraintMs=0,PostContactMs=0;
    int32 InsideBefore=0,InsideAfter=0,MovableParticles=0;
    double PenetrationBefore=0,PenetrationAfter=0;
    double Accumulator=0;
    uint64 Generation=MAX_uint64;
    int32 ShapeRevision=INDEX_NONE,TeleportRevision=INDEX_NONE,CompletedSteps=0;
    int32 DebugSide=INDEX_NONE;
    float DebugDepth=0,AppliedDebugDepth=0;
    bool bConstraintsAdded=false;
    bool bReleasingDebug=false;
};

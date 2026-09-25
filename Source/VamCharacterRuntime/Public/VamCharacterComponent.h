#pragma once
#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "VamShapeData.h"
#include "VamCharacterComponent.generated.h"
class UVamCharacterDefinition;
class USkeletalMeshComponent;
class UVamRigProfile;
class UPhysicsAsset;
class UVamAppearancePreset;
class UVamMaterialProfile;
class UVamShapeAnimInstance;
struct FStreamableHandle;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FVamCharacterLoadResult, bool, Success, const FString&, Message);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FVamShapeChanged, const FVamShapeChange&, Change);

UCLASS(ClassGroup=(VaM), meta=(BlueprintSpawnableComponent))
class VAMCHARACTERRUNTIME_API UVamCharacterComponent : public USceneComponent
{
    GENERATED_BODY()
public:
    UVamCharacterComponent();
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VaM") TSoftObjectPtr<class UVamRuntimeConfiguration> RuntimeConfiguration;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VaM") TSoftObjectPtr<UVamCharacterDefinition> Definition;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VaM|Rig") TSoftObjectPtr<UVamRigProfile> RigProfile;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VaM|Rig") TSoftClassPtr<UVamShapeAnimInstance> AnimationClass;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VaM|Animation") TSoftObjectPtr<class UAnimSequence> BaseAnimation;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VaM|Physics") TSoftObjectPtr<UPhysicsAsset> PhysicsAsset;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VaM|Physics") TSoftObjectPtr<class UVamPhysicsShapeProfile> PhysicsShapeProfile;
    UPROPERTY(BlueprintReadOnly, Transient, Category="VaM|Physics") int32 CollisionShapeRevision=INDEX_NONE;
    UPROPERTY(BlueprintReadOnly, Transient, Category="VaM|Physics") FString LastShapeError;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VaM|Appearance") TSoftObjectPtr<UVamAppearancePreset> AppearancePreset;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VaM|Appearance") TSoftObjectPtr<UVamMaterialProfile> MaterialProfile;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VaM|Shape") TMap<FName,float> InitialShapeValues;
    UPROPERTY(BlueprintReadOnly, Transient, Category="VaM") TObjectPtr<USkeletalMeshComponent> Body;
    UPROPERTY(BlueprintAssignable, Category="VaM") FVamCharacterLoadResult OnLoaded;
    UFUNCTION(BlueprintCallable, Category="VaM") void LoadCharacter();
    UFUNCTION(BlueprintCallable, Category="VaM") void UnloadCharacter();
    uint64 GetLoadGeneration() const { return Generation; }
    UFUNCTION(BlueprintCallable, Category="VaM") bool SetParameter(FName Name, float Value);
    /** Complete transient expression layer. Strongest absolute closure wins over authored
        expression values; empty clears it. Shape/Pose parameters are rejected atomically. */
    UFUNCTION(BlueprintCallable, Category="VaM|Expression") bool SetExpressionWeights(const TMap<FName,float>& Values);
    UPROPERTY(BlueprintAssignable, Category="VaM|Shape") FVamShapeChanged OnShapeChanged;
    UFUNCTION(BlueprintPure, Category="VaM|Shape") FVamShapeState GetShapeState(bool Committed = false) const;
    UFUNCTION(BlueprintPure, Category="VaM|Shape") FVamCharacterState GetCharacterState() const;
    UFUNCTION(BlueprintCallable, Category="VaM|Shape") bool PreviewParameters(const TMap<FName,float>& Values);
    UFUNCTION(BlueprintCallable, Category="VaM|Shape") bool CommitShape();
    UFUNCTION(BlueprintCallable, Category="VaM|Shape") void CancelShape();
    UFUNCTION(BlueprintCallable, Category="VaM|Shape") void ResetToImportedAppearance();
    UFUNCTION(BlueprintCallable, Category="VaM|Shape") void ResetToBaseShape();
    UFUNCTION(BlueprintPure, Category="VaM|Shape") TArray<FTransform> GetShapeReferencePose() const { return ShapeReferencePose; }
    /** Transient pose probe: never writes to the imported definition or mesh. */
    UFUNCTION(BlueprintCallable, Category="VaM|Debug")
    bool SetDebugBoneOffset(int32 BoneIndex, const FTransform& Offset);
    UFUNCTION(BlueprintPure, Category="VaM|Debug")
    FTransform GetDebugBoneOffset(int32 BoneIndex) const;
    UFUNCTION(BlueprintCallable, Category="VaM|Debug")
    void ResetDebugBoneOffsets();
    /** Rotate a mapped body joint relative to its shape-adjusted reference pose. The root is moved by Motion instead. */
    UFUNCTION(BlueprintPure, Category="VaM|Pose")
    bool IsPoseControlBone(int32 BoneIndex) const;
    UFUNCTION(BlueprintCallable, Category="VaM|Pose")
    bool SetPoseControlRotation(int32 BoneIndex, FRotator LocalRotation);
    UFUNCTION(BlueprintPure, Category="VaM|Pose")
    FRotator GetPoseControlRotation(int32 BoneIndex) const;
    UFUNCTION(BlueprintCallable, Category="VaM|Pose")
    void ResetPoseControlRotations();
    UFUNCTION(BlueprintCallable, Category="VaM|IK") bool SetIKGoal(FName Semantic, const FTransform& WorldGoal);
    UFUNCTION(BlueprintCallable, Category="VaM|IK") void ClearIKGoal(FName Semantic);
    UFUNCTION(BlueprintCallable, Category="VaM|IK") bool SetFootLocked(FName FootSemantic, bool bLocked);
    UFUNCTION(BlueprintPure, Category="VaM|IK") bool GetFootContactGoal(FName FootSemantic, FTransform& Goal) const;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VaM|IK") float FootProbeAboveCm=40;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VaM|IK") float FootProbeBelowCm=60;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VaM|IK") float MaximumGroundSlopeDegrees=50;
    UFUNCTION(BlueprintCallable, Category="VaM|Appearance") bool SetAppearanceScalar(FName Parameter, float Value);
    UFUNCTION(BlueprintCallable, Category="VaM|Appearance") bool SetAppearanceColor(FName Parameter, FLinearColor Value);
protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
private:
    void LoadDefinition(uint64 Ticket);
    void LoadMeshes(uint64 Ticket);
    void Assemble(uint64 Ticket);
    UPROPERTY(Transient) TObjectPtr<UVamCharacterDefinition> LoadedDefinition;
    UPROPERTY(Transient) TObjectPtr<UVamRuntimeConfiguration> LoadedRuntimeConfiguration;
    UPROPERTY(Transient) TArray<TObjectPtr<USkeletalMeshComponent>> LoadedParts;
    TSharedPtr<FStreamableHandle> Pending;
    UPROPERTY(Transient) FVamShapeState PreviewState;
    UPROPERTY(Transient) FVamShapeState CommittedState;
    UPROPERTY(Transient) TArray<FTransform> ShapeReferencePose;
    /** Instance-owned pose handles survive shape transactions without restarting animation. */
    UPROPERTY(Transient) TMap<int32,FRotator> PoseControlRotations;
    UPROPERTY(Transient) TMap<FName,float> AppearanceScalars;
    UPROPERTY(Transient) TMap<FName,FLinearColor> AppearanceColors;
    UPROPERTY(Transient) TMap<FName,float> ExpressionWeights;
    UPROPERTY(Transient) TObjectPtr<UPhysicsAsset> InstancePhysics;
    struct FFootContact
    {
        FTransform Goal;
        FVector Anchor=FVector::ZeroVector;
        TWeakObjectPtr<class UPrimitiveComponent> Support;
        FVector SupportLocalPoint=FVector::ZeroVector;
        FVector Normal=FVector::UpVector;
        bool Valid=false;
    };
    TMap<FName,FFootContact> FootContacts;
    int32 FootTeleportRevision=0;
    bool UpdateFootContact(FName Semantic, FFootContact& Contact);
    void ApplyMorphWeights();
    void ApplyAppearanceState();
    bool ApplyShape(const TArray<FName>& Changed, bool bCommitted);
    uint64 Generation = 0;
};

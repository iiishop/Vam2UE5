#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "VamCharacterActor.generated.h"
class UVamCharacterComponent;
class UVamMotionComponent;
class UVamInteractionComponent;
class UPhysicsHandleComponent;
class UPhysicalAnimationComponent;
class UVamActivePoseComponent;

/** Standard runtime character host; optional capabilities are selected by RuntimeConfiguration. */
UCLASS(Blueprintable)
class VAMCHARACTERRUNTIME_API AVamCharacterActor : public AActor
{
    GENERATED_BODY()
public:
    AVamCharacterActor();
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="VaM") TObjectPtr<UVamCharacterComponent> Character;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="VaM") TObjectPtr<UVamMotionComponent> Motion;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="VaM") TObjectPtr<UVamInteractionComponent> Interaction;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="VaM") TObjectPtr<UPhysicsHandleComponent> PhysicsHandle;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="VaM") TObjectPtr<UPhysicalAnimationComponent> PhysicalAnimation;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="VaM") TObjectPtr<UVamActivePoseComponent> ActivePose;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="VaM") TObjectPtr<class UVamPhysicsOutputComponent> PhysicsOutput;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="VaM") TObjectPtr<class UVamBreastContactComponent> BreastContact;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="VaM") TObjectPtr<class UVamBodyContactResponseComponent> BodyContactResponse;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="VaM|Hair") TObjectPtr<class UVamHairComponent> Hair;
    UFUNCTION(BlueprintCallable, Category="VaM|Breast Contact") void SetBreastContactEnabled(bool bEnabled);
    UFUNCTION(BlueprintPure, Category="VaM|Breast Contact") bool IsBreastContactEnabled() const;
    UFUNCTION(BlueprintCallable, Category="VaM|Lower Body Contact") void SetLowerBodyContactEnabled(bool bEnabled);
    /** Writes a unique fixed-pose diagnostic bundle under Saved/VamDeformationAudit. */
    UFUNCTION(CallInEditor, BlueprintCallable, Category="VaM|Diagnostics") void ExportDeformationAudit();
    /** Isolated offline geometry replay only; never modifies the production rig. */
    UPROPERTY(Transient, EditAnywhere, BlueprintReadWrite, Category="VaM|Diagnostics") FString AuditPoseOverrideJson;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="VaM|Diagnostics") FString DeformationAuditStatus;
    /** Explicit reset for OFF/ON experiments. Clears contact and all jiggle histories. */
    UFUNCTION(CallInEditor, BlueprintCallable, Category="VaM|Diagnostics") void ResetDeformationAuditState();
    UFUNCTION(CallInEditor, BlueprintCallable, Category="VaM") void LoadCharacter();
};

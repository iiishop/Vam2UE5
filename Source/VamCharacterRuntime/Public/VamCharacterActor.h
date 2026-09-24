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

/** Blueprintable minimal assembly host; intentionally no character movement or physics. */
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
    UFUNCTION(CallInEditor, BlueprintCallable, Category="VaM") void LoadCharacter();
};

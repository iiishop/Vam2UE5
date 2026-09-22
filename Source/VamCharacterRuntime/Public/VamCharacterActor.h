#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "VamCharacterActor.generated.h"
class UVamCharacterComponent;

/** Blueprintable minimal assembly host; intentionally no character movement or physics. */
UCLASS(Blueprintable)
class VAMCHARACTERRUNTIME_API AVamCharacterActor : public AActor
{
    GENERATED_BODY()
public:
    AVamCharacterActor();
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="VaM") TObjectPtr<UVamCharacterComponent> Character;
    UFUNCTION(CallInEditor, BlueprintCallable, Category="VaM") void LoadCharacter();
};

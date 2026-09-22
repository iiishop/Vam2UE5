#pragma once
#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "VamCharacterComponent.generated.h"
class UVamCharacterDefinition;
class USkeletalMeshComponent;
struct FStreamableHandle;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FVamCharacterLoadResult, bool, Success, const FString&, Message);

UCLASS(ClassGroup=(VaM), meta=(BlueprintSpawnableComponent))
class VAMCHARACTERRUNTIME_API UVamCharacterComponent : public USceneComponent
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VaM") TSoftObjectPtr<UVamCharacterDefinition> Definition;
    UPROPERTY(BlueprintReadOnly, Transient, Category="VaM") TObjectPtr<USkeletalMeshComponent> Body;
    UPROPERTY(BlueprintAssignable, Category="VaM") FVamCharacterLoadResult OnLoaded;
    UFUNCTION(BlueprintCallable, Category="VaM") void LoadCharacter();
    UFUNCTION(BlueprintCallable, Category="VaM") void UnloadCharacter();
    UFUNCTION(BlueprintCallable, Category="VaM") bool SetParameter(FName Name, float Value);
protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
    void LoadMeshes(uint64 Ticket);
    void Assemble(uint64 Ticket);
    UPROPERTY(Transient) TObjectPtr<UVamCharacterDefinition> LoadedDefinition;
    UPROPERTY(Transient) TArray<TObjectPtr<USkeletalMeshComponent>> LoadedParts;
    TSharedPtr<FStreamableHandle> Pending;
    uint64 Generation = 0;
};

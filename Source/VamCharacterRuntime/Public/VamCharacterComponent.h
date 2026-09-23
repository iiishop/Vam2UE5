#pragma once
#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "VamShapeData.h"
#include "VamCharacterComponent.generated.h"
class UVamCharacterDefinition;
class USkeletalMeshComponent;
struct FStreamableHandle;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FVamCharacterLoadResult, bool, Success, const FString&, Message);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FVamShapeChanged, const FVamShapeChange&, Change);

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
    UPROPERTY(BlueprintAssignable, Category="VaM|Shape") FVamShapeChanged OnShapeChanged;
    UFUNCTION(BlueprintPure, Category="VaM|Shape") FVamShapeState GetShapeState(bool Committed = false) const;
    UFUNCTION(BlueprintPure, Category="VaM|Shape") FVamCharacterState GetCharacterState() const;
    UFUNCTION(BlueprintCallable, Category="VaM|Shape") bool PreviewParameters(const TMap<FName,float>& Values);
    UFUNCTION(BlueprintCallable, Category="VaM|Shape") bool CommitShape();
    UFUNCTION(BlueprintCallable, Category="VaM|Shape") void CancelShape();
    UFUNCTION(BlueprintCallable, Category="VaM|Shape") void ResetToImportedAppearance();
    UFUNCTION(BlueprintCallable, Category="VaM|Shape") void ResetToBaseShape();
    UFUNCTION(BlueprintPure, Category="VaM|Shape") TArray<FTransform> GetShapeReferencePose() const { return ShapeReferencePose; }
protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
    void LoadMeshes(uint64 Ticket);
    void Assemble(uint64 Ticket);
    UPROPERTY(Transient) TObjectPtr<UVamCharacterDefinition> LoadedDefinition;
    UPROPERTY(Transient) TArray<TObjectPtr<USkeletalMeshComponent>> LoadedParts;
    TSharedPtr<FStreamableHandle> Pending;
    UPROPERTY(Transient) FVamShapeState PreviewState;
    UPROPERTY(Transient) FVamShapeState CommittedState;
    UPROPERTY(Transient) TArray<FTransform> ShapeReferencePose;
    void ApplyShape(const TArray<FName>& Changed, bool bCommitted);
    uint64 Generation = 0;
};

#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "VamMetaHumanComponent.generated.h"

/** Small, instance-local bridge attached to an official assembled BP. Never owns MH animation. */
UCLASS(ClassGroup=(VaM), meta=(BlueprintSpawnableComponent))
class VAMCHARACTERRUNTIME_API UVamMetaHumanComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="MetaHuman") FGuid InstanceId;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="MetaHuman") int32 ShapeRevision=0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="MetaHuman") int32 SurfaceRevision=0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="MetaHuman") int32 EquipmentRevision=0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="MetaHuman") FString LastError;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="MetaHuman") bool Ready=false;
    UFUNCTION(BlueprintCallable, Category="MetaHuman") void NotifySurfaceChanged() { ++SurfaceRevision; }
    UFUNCTION(BlueprintCallable, Category="MetaHuman") void NotifyEquipmentChanged() { ++EquipmentRevision; }
    // Notification only: runtime shape generation/physics rebinding is deliberately not implemented in MH00.
    UFUNCTION(BlueprintCallable, Category="MetaHuman") void NotifyShapeChanged() { ++ShapeRevision; }
protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
};

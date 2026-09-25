#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "VamSoftTissueRuntimeProbe.generated.h"

/** Verification only. Removing this actor cannot remove any character runtime capability. */
UCLASS()
class VAMCHARACTERRUNTIME_API AVamSoftTissueRuntimeProbe : public AActor
{
    GENERATED_BODY()
public:
    AVamSoftTissueRuntimeProbe();
    UPROPERTY(EditAnywhere, Category="Test") TArray<TObjectPtr<class AVamCharacterActor>> Subjects;
protected:
    virtual void Tick(float DeltaSeconds) override;
private:
    int32 Phase=0,Cycles=0;
    double Started=0,PhaseTime=0;
    double AnimationBeforeOff=0;
    int32 PeerShape=INDEX_NONE;
    FGuid PeerIdentity;
    FName Parameter;
    float Value=0;
    UPROPERTY(Transient) TObjectPtr<class AVamCharacterActor> Spawned;
    TArray<TWeakObjectPtr<UActorComponent>> DestroyedComponents;
    void Finish(bool Passed,const FString& Reason);
};

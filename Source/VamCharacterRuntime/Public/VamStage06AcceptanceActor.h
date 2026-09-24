#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "VamStage06AcceptanceActor.generated.h"

/** Dormant unless -VamStage06Acceptance is supplied to a standalone/game launch. */
UCLASS()
class VAMCHARACTERRUNTIME_API AVamStage06AcceptanceActor : public AActor
{
    GENERATED_BODY()
public:
    AVamStage06AcceptanceActor();
protected:
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
private:
    void Finish(bool bPassed, const FString& Reason);
    bool bEnabled=false;
    int32 Phase=0;
    double Started=0;
    double PhaseTime=0;
    FVector InitialHand=FVector::ZeroVector;
    FVector OtherHand=FVector::ZeroVector;
    FVector GrabStart=FVector::ZeroVector;
    FTransform InitialRoot;
    float MaxWitness=0;
    float MaxSpeed=0;
    float MaxBlinkLeft=0;
    float MaxBlinkRight=0;
    UPROPERTY(Transient) TObjectPtr<class AVamCharacterActor> Primary;
    UPROPERTY(Transient) TObjectPtr<class AVamCharacterActor> Secondary;
};

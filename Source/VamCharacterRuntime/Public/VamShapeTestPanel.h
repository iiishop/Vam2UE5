#pragma once
#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "GameFramework/GameModeBase.h"
#include "VamShapeTestPanel.generated.h"
class UVamCharacterComponent;

/** Small runtime acceptance panel; owns no shape state and edits the selected instance only. */
UCLASS()
class VAMCHARACTERRUNTIME_API AVamShapeTestPanel : public AHUD
{
    GENERATED_BODY()
public:
    virtual void DrawHUD() override;
    virtual void NotifyHitBoxClick(FName BoxName) override;
private:
    int32 SelectedInstance=0;
    UPROPERTY(Transient) TObjectPtr<UVamCharacterComponent> Selected;
    TArray<FName> Rows;
};

UCLASS()
class VAMCHARACTERRUNTIME_API AVamShapeTestGameMode : public AGameModeBase
{
    GENERATED_BODY()
public:
    AVamShapeTestGameMode();
    virtual void Tick(float DeltaSeconds) override;
private:
    int32 TestPhase=0;
    double TestTime=0;
    bool bSelfTestReady=false;
    TArray<FVector3f> InitialVertices;
    TArray<FVector3f> OtherVertices;
};

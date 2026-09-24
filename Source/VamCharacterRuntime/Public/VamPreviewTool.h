#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "VamPreviewTool.generated.h"

/** Place once in a preview level. PIE mouse: left bone pose/grab, right root drag. */
UCLASS(Blueprintable)
class VAMCHARACTERRUNTIME_API AVamPreviewTool : public AActor
{
    GENERATED_BODY()
public:
    AVamPreviewTool();
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VaM|Preview") bool bShowBoneControls = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VaM|Preview") float HitRadiusPixels = 12.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VaM|Preview") TObjectPtr<class AVamCharacterActor> Target;
    UFUNCTION(BlueprintCallable, Category="VaM|Preview") void SetTarget(AVamCharacterActor* Character) { Target=Character; }
protected:
    virtual void Tick(float DeltaSeconds) override;
private:
    bool FindControlAtMouse(class APlayerController* PC, AVamCharacterActor*& OutActor, int32& OutBone, FVector& OutPosition) const;
    bool MouseOnPlane(class APlayerController* PC, FVector& OutPoint) const;
    UPROPERTY(Transient) TObjectPtr<AVamCharacterActor> DragActor;
    int32 DragBone=INDEX_NONE;
    bool bRootDrag=false;
    bool bPhysicsGrab=false;
    FVector Anchor=FVector::ZeroVector;
    FVector PlaneNormal=FVector::ForwardVector;
    FTransform RootStart;
    FTransform BoneStart;
};

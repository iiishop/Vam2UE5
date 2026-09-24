#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "VamPreviewTool.generated.h"

/** Place once in a preview level. PIE mouse: root translates, other points rotate their bones. */
UCLASS(Blueprintable)
class VAMCHARACTERRUNTIME_API AVamPreviewTool : public AActor
{
    GENERATED_BODY()
public:
    AVamPreviewTool();
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VaM|Preview") bool bShowBoneControls = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VaM|Preview") float HitRadiusPixels = 14.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VaM|Preview", meta=(ClampMin="0.01",ClampMax="5.0")) float RotationDegreesPerPixel = 0.35f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VaM|Preview") TObjectPtr<class AVamCharacterActor> Target;
    UFUNCTION(BlueprintCallable, Category="VaM|Preview") void SetTarget(AVamCharacterActor* Character) { Target=Character; }
protected:
    virtual void Tick(float DeltaSeconds) override;
private:
    bool FindControlAtMouse(class APlayerController* PC, bool bRootOnly, AVamCharacterActor*& OutActor, int32& OutBone, FVector& OutPosition) const;
    bool MouseOnPlane(class APlayerController* PC, FVector& OutPoint) const;
    UPROPERTY(Transient) TObjectPtr<AVamCharacterActor> DragActor;
    TWeakObjectPtr<AVamCharacterActor> HoverActor;
    int32 HoverBone=INDEX_NONE;
    int32 DragBone=INDEX_NONE;
    bool bRootDrag=false;
    bool bRootRotationDrag=false;
    FVector Anchor=FVector::ZeroVector;
    FVector PlaneNormal=FVector::ForwardVector;
    FVector2D MouseStart=FVector2D::ZeroVector;
    FRotator PoseStart=FRotator::ZeroRotator;
    FTransform RootStart;
};

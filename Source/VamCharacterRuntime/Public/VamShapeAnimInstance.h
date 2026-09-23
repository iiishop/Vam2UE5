#pragma once
#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "VamShapeAnimInstance.generated.h"

/** Evaluates the component's shape reference, including its private inverse bind. */
UCLASS(Transient, Blueprintable)
class VAMCHARACTERRUNTIME_API UVamShapeAnimInstance : public UAnimInstance
{
    GENERATED_BODY()
protected:
    virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
    virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* Proxy) override;
};

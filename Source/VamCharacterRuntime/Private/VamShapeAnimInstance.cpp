#include "VamShapeAnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimNodeBase.h"
class FVamShapeProxy final : public FAnimInstanceProxy
{
public:
    explicit FVamShapeProxy(UAnimInstance* Instance) : FAnimInstanceProxy(Instance) {}
    virtual bool Evaluate(FPoseContext& Output) override { Output.ResetToRefPose(); return true; }
};
FAnimInstanceProxy* UVamShapeAnimInstance::CreateAnimInstanceProxy() { return new FVamShapeProxy(this); }
void UVamShapeAnimInstance::DestroyAnimInstanceProxy(FAnimInstanceProxy* Proxy) { delete Proxy; }

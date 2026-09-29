#pragma once
#include "CoreMinimal.h"

/** Units: cm/s^2. ReferenceLocal belongs to the immutable imported anatomical frame. */
namespace VamSecondaryGravity
{
    inline FVector ResidualLocal(const FVector& WorldGravity,const FQuat& Frame,const FVector& ReferenceLocal)
    {
        return Frame.UnrotateVector(WorldGravity)-ReferenceLocal;
    }
    inline FVector PreloadWorld(const FQuat& Frame,const FVector& ReferenceLocal)
    {
        return -Frame.RotateVector(ReferenceLocal);
    }
}

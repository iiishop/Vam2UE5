#pragma once
#include "VamGluteStructureProfile.h"

struct VAMCHARACTERRUNTIME_API FVamGluteRegionState
{
    FTransform Transform;
    FVector ThighPoint=FVector::ZeroVector;
    double Tension=0, PelvisAttachment=0, ThighAttachment=0, Support=0;
};
struct VAMCHARACTERRUNTIME_API FVamGluteStructuralState
{
    FVector HipAnglesDegrees=FVector::ZeroVector; // flexion, abduction, external rotation
    TArray<FVamGluteRegionState> Regions;
};
namespace VamGluteStructure
{
    VAMCHARACTERRUNTIME_API FQuat FiberBasis(const FVamGluteSide& S,const FVamGluteRegion& R);
    /** Pure pose function: deliberately no clock, delta time, velocity or mutable solver. */
    VAMCHARACTERRUNTIME_API FVamGluteStructuralState Evaluate(const UVamGluteStructureProfile& P,const FVamGluteSide& S,const FTransform& ThighInAnchor);
    VAMCHARACTERRUNTIME_API void ApplyShape(FVamGluteSide& S,const TMap<FName,float>& Values);
}

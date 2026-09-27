#pragma once
#include "CoreMinimal.h"
#include "VamBreastJiggleProfile.h"

/** Instance tuning in anatomical AP / ML / SI axes. Multipliers preserve authored node differences. */
struct VAMCHARACTERRUNTIME_API FVamBreastTuning
{
    FVector FrequencyScale=FVector(1), DampingScale=FVector(1), TravelScale=FVector(1);
    double CouplingScale=1;
    FVamBreastSideProfile DynamicsRest(const FVamBreastSideProfile& Source) const;
};

struct VAMCHARACTERRUNTIME_API FVamBreastNodeState
{
    FVector Displacement=FVector::ZeroVector, Velocity=FVector::ZeroVector;
};
/** Value-only per-instance solver. No UObject access, shared state, or rendering dependency. */
struct VAMCHARACTERRUNTIME_API FVamBreastSolver
{
    TArray<FVamBreastNodeState> Nodes;
    FVector LinearVelocity=FVector::ZeroVector, LinearAcceleration=FVector::ZeroVector;
    FVector AngularVelocity=FVector::ZeroVector, AngularAcceleration=FVector::ZeroVector;
    FTransform PreviousFrame;
    FVector PreviousVelocity=FVector::ZeroVector, PreviousOmega=FVector::ZeroVector;
    double Accumulator=0;
    int32 Samples=0, LastSteps=0, DroppedSteps=0;
    bool bSleeping=false;
    void Reset(bool Preserve=false);
    void Advance(const UVamBreastJiggleProfile& Profile,const FVamBreastSideProfile& Rest,
        const FTransform& Frame,double Dt,const FVector& GravityWorld,bool ResetHistory,bool Paused,double Softness=1,double CouplingScale=1);
    void Step(const UVamBreastJiggleProfile& Profile,const FVamBreastSideProfile& Rest,
        double Dt,const FVector& GravityLocal,double Softness=1,double CouplingScale=1);
};

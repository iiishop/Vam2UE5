#pragma once
#include "CoreMinimal.h"
#include "VamBreastJiggleProfile.h"

/** Instance tuning in anatomical AP / ML / SI axes. Multipliers preserve authored node differences. */
struct VAMCHARACTERRUNTIME_API FVamBreastTuning
{
    double Support=1,Damping=1,Mobility=1,InternalCoupling=1,MassScale=1;
    double LegacyCompliance=1;
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
    FVector AngularDisplacement=FVector::ZeroVector, RelativeAngularVelocity=FVector::ZeroVector;
    int32 LimitCorrections=0;
    FTransform PreviousFrame;
    FVector PreviousVelocity=FVector::ZeroVector, PreviousOmega=FVector::ZeroVector;
    double Accumulator=0;
    int32 Samples=0, LastSteps=0, DroppedSteps=0;
    bool bSleeping=false;
    void Reset(bool Preserve=false);
    void Advance(const UVamBreastJiggleProfile& Profile,const FVamBreastSideProfile& Rest,
        const FTransform& Frame,double Dt,const FVector& GravityWorld,bool ResetHistory,bool Paused,const FVamBreastTuning& Tuning=FVamBreastTuning());
    void Step(const UVamBreastJiggleProfile& Profile,const FVamBreastSideProfile& Rest,
        double Dt,const FVector& GravityLocal,const FVamBreastTuning& Tuning=FVamBreastTuning());
    void StepLegacy(const UVamBreastJiggleProfile&,const FVamBreastSideProfile&,double,const FVector&,double,double);
    void StepCalibrated(const UVamBreastJiggleProfile&,const FVamBreastSideProfile&,double,const FVector&);
};

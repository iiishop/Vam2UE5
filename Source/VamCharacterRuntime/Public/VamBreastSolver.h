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
/** A measured final chest frame. Optional world twist is useful for physics/analytic callers;
 * animation-only callers use the Transform overload and interval twist estimation. */
struct VAMCHARACTERRUNTIME_API FVamMovingFrameSample
{
    FTransform Transform=FTransform::Identity;
    FVector VelocityWorld=FVector::ZeroVector,OmegaWorld=FVector::ZeroVector;
    bool bHasTwist=false;
};
/** Value-only per-instance solver. No UObject access, shared state, or rendering dependency. */
struct VAMCHARACTERRUNTIME_API FVamBreastSolver
{
    TArray<FVamBreastNodeState> Nodes;
    FVector COMDisplacement=FVector::ZeroVector,COMVelocity=FVector::ZeroVector;
    FVector CentrifugalLoad=FVector::ZeroVector;
    double TranslationLoadMagnitude=0,RotationLoadMagnitude=0,ResidualLoadMagnitude=0;
    FVector LinearVelocity=FVector::ZeroVector, LinearAcceleration=FVector::ZeroVector;
    FVector AngularVelocity=FVector::ZeroVector, AngularAcceleration=FVector::ZeroVector;
    FVector AngularDisplacement=FVector::ZeroVector, RelativeAngularVelocity=FVector::ZeroVector;
    int32 LimitCorrections=0;
    FTransform PreviousFrame;
    FVector PhysicsVelocityWorld=FVector::ZeroVector,PhysicsOmegaWorld=FVector::ZeroVector;
    FVector PreviousVelocity=FVector::ZeroVector, PreviousOmega=FVector::ZeroVector;
    FVector PreviousIntervalVelocity=FVector::ZeroVector,PreviousIntervalOmega=FVector::ZeroVector;
    double PreviousDt=0;
    TArray<FVector> PositionHistory;
    TArray<double> TimeHistory;
    double Accumulator=0;
    int32 Samples=0, LastSteps=0, DroppedSteps=0;
    bool bSleeping=false;
    void Reset(bool Preserve=false);
    void AdvanceFrame(const UVamBreastJiggleProfile&,const FVamBreastSideProfile&,const FVamMovingFrameSample&,double,const FVector&,bool,bool,const FVamBreastTuning& Tuning=FVamBreastTuning());
    void ApplyFrameVelocityChange(const FVamBreastSideProfile&,const FVector& DeltaVelocityLocal,const FVector& DeltaOmegaLocal);
    void ProjectResidual(const FVamBreastSideProfile&);
    FVector NodeOffset(const FVamBreastSideProfile&,int32 Index) const;
    FVector NodeRelativeVelocity(const FVamBreastSideProfile&,int32 Index) const;
    void Advance(const UVamBreastJiggleProfile& Profile,const FVamBreastSideProfile& Rest,
        const FTransform& Frame,double Dt,const FVector& GravityWorld,bool ResetHistory,bool Paused,const FVamBreastTuning& Tuning=FVamBreastTuning());
    void Step(const UVamBreastJiggleProfile& Profile,const FVamBreastSideProfile& Rest,
        double Dt,const FVector& GravityLocal,const FVamBreastTuning& Tuning=FVamBreastTuning());
    void StepLegacy(const UVamBreastJiggleProfile&,const FVamBreastSideProfile&,double,const FVector&,double,double);
    void AdvanceLegacy(const UVamBreastJiggleProfile&,const FVamBreastSideProfile&,const FTransform&,double,const FVector&,bool,bool,const FVamBreastTuning&);
    void StepCalibrated(const UVamBreastJiggleProfile&,const FVamBreastSideProfile&,double,const FVector&);
};

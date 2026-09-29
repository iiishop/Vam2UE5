#pragma once
#include "VamGluteJiggleProfile.h"

struct VAMCHARACTERRUNTIME_API FVamGluteTuning
{
    double Support=1,Damping=1,Mobility=1,InternalCoupling=1,MassScale=1;
};
struct VAMCHARACTERRUNTIME_API FVamGluteMotion
{
    FTransform Pelvis=FTransform::Identity,Thigh=FTransform::Identity; // anatomical pelvis anchor and primary femur, WORLD
    FVector PelvisVelocity=FVector::ZeroVector,PelvisOmega=FVector::ZeroVector;
    FVector ThighVelocity=FVector::ZeroVector,ThighOmega=FVector::ZeroVector;
    bool bKnownTwist=false;
    FVector RestVelocity[5]={}; // structural rest derivative in anatomical anchor coordinates
};
struct VAMCHARACTERRUNTIME_API FVamGluteParticle
{
    FVector PositionWorld=FVector::ZeroVector,VelocityWorld=FVector::ZeroVector;
    FVector Displacement=FVector::ZeroVector,RelativeVelocity=FVector::ZeroVector;
    FVector RestWorld=FVector::ZeroVector,ThighTargetVelocity=FVector::ZeroVector;
    FVector Support=FVector::ZeroVector,Damping=FVector::ZeroVector,Travel=FVector::ZeroVector;
    double Mass=0,NonlinearTravel=0;
};
/** Five world particles with prescribed pelvis/femur attachments. G0 remains the
 * loaded equilibrium. No angular mode, UObject state or per-vertex simulation. */
struct VAMCHARACTERRUNTIME_API FVamGluteSolver
{
    FVamGluteParticle Nodes[5];
    FVamGluteMotion Previous;
    FVamGluteDynamicSide PreviousRest;
    FVector LinearVelocity=FVector::ZeroVector,LinearAcceleration=FVector::ZeroVector,Omega=FVector::ZeroVector,Alpha=FVector::ZeroVector;
    FVector GravityForce=FVector::ZeroVector,GravityPreload=FVector::ZeroVector;
    FVector IntervalV=FVector::ZeroVector,IntervalW=FVector::ZeroVector,ThighIntervalV=FVector::ZeroVector,ThighIntervalW=FVector::ZeroVector;
    double Accumulator=0,PreviousDt=0,LastCostMicroseconds=0;
    int32 Samples=0,LastSteps=0,DroppedSteps=0,LimitCorrections=0;
    bool bSleeping=false,bResume=false,bInitializeWorldVelocity=true;
    void Reset();
    void Advance(const UVamGluteJiggleProfile&,const FVamGluteDynamicSide&,FVamGluteMotion,double,const FVector&,const FVamGluteTuning&,bool Teleport=false,bool Paused=false,bool ShapeRebase=false);
    void Step(const UVamGluteJiggleProfile&,const FVamGluteDynamicSide&,const FVamGluteMotion&,double,const FVector&,const FVamGluteTuning&);
};

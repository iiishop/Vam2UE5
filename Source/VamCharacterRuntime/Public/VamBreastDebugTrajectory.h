#pragma once
#include "CoreMinimal.h"

/** Analytic motion fixtures. They only move the actor; solver has no command/phase input. */
struct VAMCHARACTERRUNTIME_API FVamMotionRamp
{
    double Coefficients[6]={};
    double Duration=1,Target=0;
    void Start(double Velocity,double Acceleration,double Jerk,double EndVelocity,double Seconds);
    void Evaluate(double Time,double& Position,double& Velocity,double& Acceleration,double& Jerk) const;
};
struct VAMCHARACTERRUNTIME_API FVamJumpTrajectory
{
    double Gravity=980,Takeoff=.25,Landing=.25,LaunchSpeed=250;
    double FlightTime() const;
    double Duration() const { return Takeoff+FlightTime()+Landing; }
    void Evaluate(double Time,double& Height,double& Velocity,double& Acceleration,double& Jerk) const;
};

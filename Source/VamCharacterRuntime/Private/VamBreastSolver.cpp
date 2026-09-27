#include "VamBreastSolver.h"

namespace
{
double TuningValue(double Value,double Minimum,double Maximum)
{
    return FMath::Clamp(FMath::IsFinite(Value)?Value:1.,Minimum,Maximum);
}
}
FVamBreastSideProfile FVamBreastTuning::DynamicsRest(const FVamBreastSideProfile& Source) const
{
    // Only solver inputs: never copy dense region weights/points during pose evaluation.
    FVamBreastSideProfile Result;
    Result.MassKg=Source.MassKg;Result.ReferenceMassKg=Source.ReferenceMassKg;
    Result.ImportedGravityLocal=Source.ImportedGravityLocal;Result.Nodes=Source.Nodes;
    for(auto& Node:Result.Nodes) for(int32 Axis=0;Axis<3;++Axis)
    {
        Node.FrequencyHz[Axis]*=TuningValue(FrequencyScale[Axis],.2,3.);
        Node.DampingRatio[Axis]*=TuningValue(DampingScale[Axis],.05,4.);
        Node.PositiveLimitCm[Axis]*=TuningValue(TravelScale[Axis],.25,4.);
        Node.NegativeLimitCm[Axis]*=TuningValue(TravelScale[Axis],.25,4.);
    }
    return Result;
}

bool UVamBreastJiggleProfile::IsValidProfile() const
{
    if(SchemaVersion!=1 || Sides.Num()!=2 || SourceTopologyIdentity.IsEmpty() || DensityKgPerCm3<=0 ||
        !FMath::IsFinite(DensityKgPerCm3) || !FMath::IsFinite(FixedStep) || !FMath::IsFinite(CouplingHz) || CouplingHz<0 || CouplingHz>15 ||
        !FMath::IsFinite(SoftLimitFraction) || SoftLimitFraction<=0 || SoftLimitFraction>=1 || FixedStep<=0 || FixedStep>1./60. || MaxSubsteps<1 || MaxSubsteps>64) return false;
    for(const auto& S:Sides)
    {
        if(S.Nodes.Num()!=5 || S.ChestBone<0 || S.AnchorBone<SourceBoneCount || S.EffectiveVolumeCm3<=0 || S.MassKg<=0 || !FMath::IsFinite(S.MassKg) || !FMath::IsFinite(S.EffectiveVolumeCm3) || S.COM.ContainsNaN() || S.AnchorLocal.ContainsNaN()) return false;
        for(const auto& N:S.Nodes)
            if(N.BoneIndex<=S.AnchorBone || N.MassFraction<=0 || N.FrequencyHz.GetMin()<=0 || N.DampingRatio.GetMin()<0 ||
                N.PositiveLimitCm.GetMin()<=0 || N.NegativeLimitCm.GetMin()<=0 || N.Rest.ContainsNaN() || N.FrequencyHz.ContainsNaN() || N.DampingRatio.ContainsNaN() || N.PositiveLimitCm.ContainsNaN() || N.NegativeLimitCm.ContainsNaN()) return false;
    }
    return true;
}
void FVamBreastSolver::Reset(bool Preserve)
{
    if(!Preserve) Nodes.Reset();
    for(auto& N:Nodes) N.Velocity=FVector::ZeroVector;
    Samples=LastSteps=0;Accumulator=0;bSleeping=false;
    PreviousVelocity=PreviousOmega=LinearVelocity=LinearAcceleration=AngularVelocity=AngularAcceleration=FVector::ZeroVector;
}
void FVamBreastSolver::Advance(const UVamBreastJiggleProfile& P,const FVamBreastSideProfile& R,
    const FTransform& Frame,double Dt,const FVector& GravityWorld,bool ResetHistory,bool Paused,double Softness,double CouplingScale)
{
    LastSteps=0;
    if(Paused || Dt<=0) { Samples=0;Accumulator=0;return; }
    if(!FMath::IsFinite(Dt) || Frame.ContainsNaN()) { Reset();return; }
    const double Angle=Frame.GetRotation().AngularDistance(PreviousFrame.GetRotation());
    if(ResetHistory || (Samples && ((Frame.GetLocation()-PreviousFrame.GetLocation()).Size()>P.TeleportDistanceCm || Angle>P.TeleportAngleRadians)) || Dt>P.FixedStep*P.MaxSubsteps*2)
        Reset(P.bPreserveDisplacementOnTeleport);
    if(Nodes.Num()!=R.Nodes.Num()) { Reset();Nodes.SetNum(R.Nodes.Num()); }
    if(!Samples) { PreviousFrame=Frame;Samples=1;return; }
    const FVector V=(Frame.GetLocation()-PreviousFrame.GetLocation())/Dt;
    FQuat Q=(Frame.GetRotation()*PreviousFrame.GetRotation().Inverse()).GetNormalized();
    if(Q.W<0) Q=Q*-1.;
    FVector Axis;double Theta;Q.ToAxisAndAngle(Axis,Theta);
    const FVector W=Axis.GetSafeNormal()*Theta/Dt;
    LinearVelocity=Frame.InverseTransformVectorNoScale(V);
    AngularVelocity=Frame.InverseTransformVectorNoScale(W);
    LinearAcceleration=Samples>1 ? Frame.InverseTransformVectorNoScale((V-PreviousVelocity)/Dt) : FVector::ZeroVector;
    AngularAcceleration=Samples>1 ? Frame.InverseTransformVectorNoScale((W-PreviousOmega)/Dt) : FVector::ZeroVector;
    PreviousFrame=Frame;PreviousVelocity=V;PreviousOmega=W;Samples=2;
    const double Accepted=FMath::Min(Dt,P.FixedStep*P.MaxSubsteps);
    DroppedSteps+=FMath::Max(0,FMath::FloorToInt((Dt-Accepted)/P.FixedStep));
    Accumulator+=Accepted;
    while(Accumulator+1.e-10>=P.FixedStep && LastSteps<P.MaxSubsteps)
    {
        Step(P,R,P.FixedStep,Frame.InverseTransformVectorNoScale(GravityWorld),Softness,CouplingScale);
        Accumulator-=P.FixedStep;++LastSteps;
    }
}
void FVamBreastSolver::Step(const UVamBreastJiggleProfile& P,const FVamBreastSideProfile& R,double H,const FVector& G,double Softness,double CouplingScale)
{
    if(Nodes.Num()!=R.Nodes.Num()) Nodes.SetNum(R.Nodes.Num());
    const auto Before=Nodes;
    double MaxSpeed=0,MaxAcceleration=0;
    for(int32 I=0;I<Nodes.Num();++I)
    {
        auto& S=Nodes[I];const auto& N=R.Nodes[I];
        const double Mass=R.MassKg*N.MassFraction;
        FVector A=G-R.ImportedGravityLocal-LinearAcceleration
            -FVector::CrossProduct(AngularAcceleration,N.Rest+S.Displacement)
            -FVector::CrossProduct(AngularVelocity,FVector::CrossProduct(AngularVelocity,N.Rest+S.Displacement));
        // Pair forces use reduced mass, so unequal semantic masses still conserve pair momentum.
        for(int32 J=0;J<Nodes.Num();++J) if(J!=I)
        {
            const double Other=R.MassKg*R.Nodes[J].MassFraction;
            // Bound explicit pair stiffness by step size, including the five-node graph.
            const double CouplingHz=FMath::Min(P.CouplingHz*FMath::Sqrt(TuningValue(CouplingScale,0.,2.)),.08/H);
            const double K=FMath::Square(2*PI*CouplingHz)*(Mass*Other/(Mass+Other));
            A+=K/Mass*(Before[J].Displacement-Before[I].Displacement);
        }
        FVector Diagonal,Right;
        for(int32 K=0;K<3;++K)
        {
            const double Limit=S.Displacement[K]>=0 ? N.PositiveLimitCm[K] : N.NegativeLimitCm[K];
            const double Ratio=FMath::Abs(S.Displacement[K])/Limit;
            const double Curve=S.Displacement[K]>=0 ? N.PositiveNonlinearity[K] : N.NegativeNonlinearity[K];
            const double Transition=FMath::Max(0.,(Ratio-P.SoftLimitFraction)/(1-P.SoftLimitFraction));
            const double W2=(R.ReferenceMassKg>0 ? R.ReferenceMassKg/R.MassKg : 1.)*FMath::Square(2*PI*N.FrequencyHz[K])/FMath::Clamp(FMath::IsFinite(Softness)?Softness:1.,.2,100.)*(1+Curve*Ratio*Ratio+30*Transition*Transition);
            const double D=2*N.DampingRatio[K]*FMath::Sqrt(W2);
            Diagonal[K]=1+H*D+H*H*W2;
            Right[K]=S.Velocity[K]+H*(A[K]-W2*S.Displacement[K]);
        }
        // Backward Euler elastic/damping terms and implicit Coriolis (skew matrix).
        // Solve the 3x3 system by reciprocal basis; avoids explicit Coriolis energy gain.
        const FVector O=AngularVelocity*(2*H);
        const FVector C0(Diagonal.X,O.Z,-O.Y),C1(-O.Z,Diagonal.Y,O.X),C2(O.Y,-O.X,Diagonal.Z);
        const double Det=FVector::DotProduct(C0,FVector::CrossProduct(C1,C2));
        S.Velocity=FVector(FVector::DotProduct(Right,FVector::CrossProduct(C1,C2)),FVector::DotProduct(C0,FVector::CrossProduct(Right,C2)),FVector::DotProduct(C0,FVector::CrossProduct(C1,Right)))/Det;
        S.Displacement+=H*S.Velocity;
        for(int32 K=0;K<3;++K)
        {
            const double Bounded=FMath::Clamp(S.Displacement[K],-N.NegativeLimitCm[K],N.PositiveLimitCm[K]);
            if(Bounded!=S.Displacement[K]) { S.Displacement[K]=Bounded;S.Velocity[K]=0; }
        }
        if(S.Displacement.ContainsNaN() || S.Velocity.ContainsNaN()) { Reset();return; }
        MaxSpeed=FMath::Max(MaxSpeed,S.Velocity.Size());
        MaxAcceleration=FMath::Max(MaxAcceleration,(S.Velocity-Before[I].Velocity).Size()/H);
    }
    bSleeping=MaxSpeed<P.SleepSpeedCmS && MaxAcceleration<P.SleepAccelerationCmS2;
    if(bSleeping) for(auto& N:Nodes) N.Velocity=FVector::ZeroVector;
}

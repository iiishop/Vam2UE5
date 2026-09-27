#include "VamBreastSolver.h"
#include "VamBreastCalibration.h"

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
    const double M=TuningValue(MassScale,.1,10.),S=TuningValue(Support,.1,10.),D=TuningValue(Damping,.1,4.),L=TuningValue(Mobility,.25,3.),C=TuningValue(InternalCoupling,0.,4.);
    Result.MassKg=Source.MassKg*M;Result.ReferenceMassKg=Source.ReferenceMassKg;
    Result.COM=Source.COM;Result.InertiaDiagonal=Source.InertiaDiagonal*M;Result.InertiaOffDiagonal=Source.InertiaOffDiagonal*M;
    Result.RotationalStiffness=Source.RotationalStiffness*S;Result.RotationalDampingRatio=Source.RotationalDampingRatio*D;
    Result.AngularLimitRadians=Source.AngularLimitRadians*L;
    Result.ImportedGravityLocal=Source.ImportedGravityLocal;Result.Nodes=Source.Nodes;Result.Couplings=Source.Couplings;
    for(auto& Node:Result.Nodes)
    {
        Node.SupportStiffness*=S;Node.DampingRatio*=D;
        Node.PositiveLimitCm*=L;Node.NegativeLimitCm*=L;
    }
    for(auto& Edge:Result.Couplings) Edge.Stiffness*=C;
    VamBreastCalibration::CalibrateCOM(Result);
    return Result;
}

bool UVamBreastJiggleProfile::IsValidProfile() const
{
    if((SchemaVersion!=1 && SchemaVersion!=2 && SchemaVersion!=3) || Sides.Num()!=2 || SourceTopologyIdentity.IsEmpty() || DensityKgPerCm3<=0 ||
        !FMath::IsFinite(DensityKgPerCm3) || !FMath::IsFinite(FixedStep) || !FMath::IsFinite(CouplingHz) || CouplingHz<0 || CouplingHz>15 ||
        !FMath::IsFinite(SoftLimitFraction) || SoftLimitFraction<=0 || SoftLimitFraction>=1 || FixedStep<=0 || FixedStep>1./60. || MaxSubsteps<1 || MaxSubsteps>64) return false;
    for(const auto& S:Sides)
    {
        if(S.Nodes.Num()!=5 || S.ChestBone<0 || S.AnchorBone<SourceBoneCount || S.EffectiveVolumeCm3<=0 || S.MassKg<=0 || !FMath::IsFinite(S.MassKg) || !FMath::IsFinite(S.EffectiveVolumeCm3) || S.COM.ContainsNaN() || S.AnchorLocal.ContainsNaN()) return false;
        for(const auto& N:S.Nodes)
            if(N.BoneIndex<=S.AnchorBone || N.MassFraction<=0 || N.FrequencyHz.GetMin()<=0 || N.DampingRatio.GetMin()<0 ||
                N.PositiveLimitCm.GetMin()<=0 || N.NegativeLimitCm.GetMin()<=0 || N.Rest.ContainsNaN() || N.FrequencyHz.ContainsNaN() || N.DampingRatio.ContainsNaN() || N.PositiveLimitCm.ContainsNaN() || N.NegativeLimitCm.ContainsNaN()) return false;
        if(SchemaVersion>=3 && (S.COMSupport.ContainsNaN() || S.COMSupport.GetMin()<=0 || S.COMDampingRatio.ContainsNaN() || S.COMDampingRatio.GetMin()<0 || S.COMPositiveLimit.ContainsNaN() || S.COMPositiveLimit.GetMin()<=0 || S.COMNegativeLimit.ContainsNaN() || S.COMNegativeLimit.GetMin()<=0)) return false;
        if(SchemaVersion>=2)
        {
            if(S.InertiaDiagonal.ContainsNaN() || S.InertiaOffDiagonal.ContainsNaN() || S.InertiaDiagonal.GetMin()<=0 || S.RotationalStiffness.ContainsNaN() || S.RotationalStiffness.GetMin()<=0 || S.AngularLimitRadians.ContainsNaN() || S.AngularLimitRadians.GetMin()<=0 || S.SizeCm.GetMin()<=0 || S.Couplings.Num()!=8) return false;
            const auto& D=S.InertiaDiagonal;const auto& O=S.InertiaOffDiagonal;
            if(D.X*D.Y-O.X*O.X<=0 || D.X*D.Y*D.Z+2*O.X*O.Y*O.Z-D.X*O.Z*O.Z-D.Y*O.Y*O.Y-D.Z*O.X*O.X<=0) return false;
            double Fractions=0;
            for(const auto& N:S.Nodes)
            {
                Fractions+=N.MassFraction;
                if(N.MassCenter.ContainsNaN() || N.SupportStiffness.ContainsNaN() || N.SupportStiffness.GetMin()<=0 || !FMath::IsFinite(N.EffectiveVolumeCm3) || N.EffectiveVolumeCm3<=0) return false;
            }
            if(FMath::Abs(Fractions-1)>1.e-6) return false;
            for(const auto& E:S.Couplings) if(E.A<0 || E.A>=5 || E.B<0 || E.B>=5 || E.A==E.B || E.Stiffness.ContainsNaN() || E.Stiffness.GetMin()<0) return false;
        }
    }
    return true;
}
void FVamBreastSolver::Reset(bool Preserve)
{
    if(!Preserve) { Nodes.Reset();AngularDisplacement=COMDisplacement=FVector::ZeroVector; }
    RelativeAngularVelocity=COMVelocity=FVector::ZeroVector;LimitCorrections=0;
    PhysicsVelocityWorld=PhysicsOmegaWorld=CentrifugalLoad=FVector::ZeroVector;
    for(auto& N:Nodes) N.Velocity=FVector::ZeroVector;
    Samples=LastSteps=0;Accumulator=PreviousDt=0;bSleeping=false;PreviousIntervalVelocity=PreviousIntervalOmega=FVector::ZeroVector;
    PositionHistory.Reset();TimeHistory.Reset();
    PreviousVelocity=PreviousOmega=LinearVelocity=LinearAcceleration=AngularVelocity=AngularAcceleration=FVector::ZeroVector;
}
void FVamBreastSolver::AdvanceLegacy(const UVamBreastJiggleProfile& P,const FVamBreastSideProfile& R,
    const FTransform& Frame,double Dt,const FVector& GravityWorld,bool ResetHistory,bool Paused,const FVamBreastTuning& Tuning)
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
        Step(P,R,P.FixedStep,Frame.InverseTransformVectorNoScale(GravityWorld),Tuning);
        Accumulator-=P.FixedStep;++LastSteps;
    }
}
void FVamBreastSolver::StepLegacy(const UVamBreastJiggleProfile& P,const FVamBreastSideProfile& R,double H,const FVector& G,double Softness,double CouplingScale)
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

void FVamBreastSolver::Step(const UVamBreastJiggleProfile& P,const FVamBreastSideProfile& R,double H,const FVector& G,const FVamBreastTuning& Tuning)
{
    if(P.SchemaVersion==1) { StepLegacy(P,R,H,G,Tuning.LegacyCompliance,1.);return; }
    const auto Effective=Tuning.DynamicsRest(R);
    StepCalibrated(P,Effective,H,G);
}

void FVamBreastSolver::Advance(const UVamBreastJiggleProfile& P,const FVamBreastSideProfile& R,const FTransform& Frame,double Dt,const FVector& G,bool Rebase,bool Paused,const FVamBreastTuning& T)
{
    if(P.SchemaVersion==1) { AdvanceLegacy(P,R,Frame,Dt,G,Rebase,Paused,T);return; }
    FVamMovingFrameSample Sample;Sample.Transform=Frame;AdvanceFrame(P,R,Sample,Dt,G,Rebase,Paused,T);
}
void FVamBreastSolver::AdvanceFrame(const UVamBreastJiggleProfile& P,const FVamBreastSideProfile& R,const FVamMovingFrameSample& Input,double Dt,const FVector& G,bool Rebase,bool Paused,const FVamBreastTuning& T)
{
    LastSteps=0;const FTransform& Frame=Input.Transform;
    if(Paused || Dt<=0) { Samples=0;Accumulator=0;return; }
    if(!FMath::IsFinite(Dt) || Frame.ContainsNaN() || Input.VelocityWorld.ContainsNaN() || Input.OmegaWorld.ContainsNaN()) { Reset();return; }
    if(Rebase || (Samples && ((Frame.GetLocation()-PreviousFrame.GetLocation()).Size()>P.TeleportDistanceCm || Frame.GetRotation().AngularDistance(PreviousFrame.GetRotation())>P.TeleportAngleRadians)) || Dt>P.FixedStep*P.MaxSubsteps*2) Reset(P.bPreserveDisplacementOnTeleport);
    if(Nodes.Num()!=R.Nodes.Num()) { Reset();Nodes.SetNum(R.Nodes.Num()); }
    if(!Samples)
    {
        PreviousFrame=Frame;PreviousVelocity=PhysicsVelocityWorld=Input.bHasTwist?Input.VelocityWorld:FVector::ZeroVector;
        PreviousOmega=PhysicsOmegaWorld=Input.bHasTwist?Input.OmegaWorld:FVector::ZeroVector;Samples=Input.bHasTwist?2:1;
        PositionHistory={Frame.GetLocation()};TimeHistory={0.};return;
    }
    FVector V=Input.VelocityWorld,W=Input.OmegaWorld;
    if(!Input.bHasTwist)
    {
        V=(Frame.GetLocation()-PreviousFrame.GetLocation())/Dt;
        FQuat Q=(Frame.GetRotation()*PreviousFrame.GetRotation().Inverse()).GetNormalized();if(Q.W<0) Q=Q*-1.;
        FVector Axis;double Angle;Q.ToAxisAndAngle(Axis,Angle);W=Axis.GetSafeNormal()*Angle/Dt;
        const FVector IntervalV=V,IntervalW=W;
        if(Samples>1 && PreviousDt>0)
        {
            V+=(IntervalV-PreviousIntervalVelocity)*(Dt/(Dt+PreviousDt));
            W+=(IntervalW-PreviousIntervalOmega)*(Dt/(Dt+PreviousDt));
        }
        PositionHistory.Add(Frame.GetLocation());TimeHistory.Add(TimeHistory.Last()+Dt);
        if(PositionHistory.Num()>5) {PositionHistory.RemoveAt(0);TimeHistory.RemoveAt(0);}
        // Causal polynomial endpoint derivative, in relative coordinates to avoid
        // cancellation far from world origin. No acceleration low-pass delay.
        V=FVector::ZeroVector;const int32 Last=TimeHistory.Num()-1;
        for(int32 I=0;I<Last;++I)
        {
            double Weight=1/(TimeHistory[I]-TimeHistory[Last]);
            for(int32 J=0;J<Last;++J) if(J!=I) Weight*=(TimeHistory[Last]-TimeHistory[J])/(TimeHistory[I]-TimeHistory[J]);
            V+=Weight*(PositionHistory[I]-PositionHistory[Last]);
        }
        PreviousIntervalVelocity=IntervalV;PreviousIntervalOmega=IntervalW;PreviousDt=Dt;
    }
    if(Samples==1) { PreviousVelocity=PhysicsVelocityWorld=V;PreviousOmega=PhysicsOmegaWorld=W; }
    const FVector DiagnosticA=Frame.InverseTransformVectorNoScale((V-PreviousVelocity)/Dt),DiagnosticAlpha=Frame.InverseTransformVectorNoScale((W-PreviousOmega)/Dt);
    const double Accepted=FMath::Min(Dt,P.FixedStep*P.MaxSubsteps),OldAccumulator=Accumulator;
    DroppedSteps+=FMath::Max(0,FMath::FloorToInt((Dt-Accepted)/P.FixedStep));Accumulator+=Accepted;
    const auto Effective=T.DynamicsRest(R);
    while(Accumulator+1.e-10>=P.FixedStep && LastSteps<P.MaxSubsteps)
    {
        const double Fraction=FMath::Clamp((P.FixedStep*(LastSteps+1)-OldAccumulator)/Accepted,0.,1.);
        const FQuat Q=FQuat::Slerp(PreviousFrame.GetRotation(),Frame.GetRotation(),Fraction).GetNormalized();
        FVector StepV=FMath::Lerp(PreviousVelocity,V,Fraction);
        {
            // Cubic Hermite position reconstruction uses both measured positions and
            // endpoint velocities. Its derivative resolves ballistic intervals exactly.
            const double U=Fraction,U2=U*U;
            StepV=(6*U2-6*U)*(PreviousFrame.GetLocation()-Frame.GetLocation())/Dt
                +(3*U2-4*U+1)*PreviousVelocity+(3*U2-2*U)*V;
        }
        const FVector StepW=FMath::Lerp(PreviousOmega,W,Fraction);
        ApplyFrameVelocityChange(Effective,Q.UnrotateVector(StepV-PhysicsVelocityWorld),Q.UnrotateVector(StepW-PhysicsOmegaWorld));
        PhysicsVelocityWorld=StepV;PhysicsOmegaWorld=StepW;AngularVelocity=Q.UnrotateVector(StepW);
        // The frame acceleration has already been integrated by the velocity-change kick.
        LinearAcceleration=AngularAcceleration=FVector::ZeroVector;
        StepCalibrated(P,Effective,P.FixedStep,Q.UnrotateVector(G));
        Accumulator-=P.FixedStep;++LastSteps;
    }
    LinearVelocity=Frame.InverseTransformVectorNoScale(V);AngularVelocity=Frame.InverseTransformVectorNoScale(W);
    LinearAcceleration=DiagnosticA;AngularAcceleration=DiagnosticAlpha;
    PreviousFrame=Frame;PreviousVelocity=V;PreviousOmega=W;Samples=2;
}

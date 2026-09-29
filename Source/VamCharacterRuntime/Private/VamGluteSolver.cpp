#include "VamGluteSolver.h"
#include "VamSecondaryMath.h"
#include "VamSecondaryGravity.h"
#include "HAL/PlatformTime.h"

namespace
{
double Scale(double X,double A,double B) { return FMath::Clamp(FMath::IsFinite(X)?X:1.,A,B); }
FVector Angular(const FTransform& A,const FTransform& B,double H)
{
    FQuat Q=(B.GetRotation()*A.GetRotation().Inverse()).GetNormalized();if(Q.W<0) Q=Q*-1.;FVector Axis;double Angle;Q.ToAxisAndAngle(Axis,Angle);return Axis.GetSafeNormal()*Angle/H;
}
void Interpolate(const FTransform& A,const FTransform& B,const FVector& VA,const FVector& VB,double H,double U,FTransform& T,FVector& V)
{
    const double U2=U*U,U3=U2*U;
    T=FTransform(FQuat::Slerp(A.GetRotation(),B.GetRotation(),U).GetNormalized(),A.GetLocation()+(B.GetLocation()-A.GetLocation())*(-2*U3+3*U2)+H*((U3-2*U2+U)*VA+(U3-U2)*VB));
    V=(6*U2-6*U)*(A.GetLocation()-B.GetLocation())/H+(3*U2-4*U+1)*VA+(3*U2-2*U)*VB;
}
}
void FVamGluteSolver::Reset() { *this=FVamGluteSolver(); }
void FVamGluteSolver::Advance(const UVamGluteJiggleProfile& P,const FVamGluteDynamicSide& R,FVamGluteMotion M,double Dt,const FVector& G,const FVamGluteTuning& T,bool Teleport,bool Paused,bool ShapeRebase)
{
    const double Start=FPlatformTime::Seconds();LastSteps=0;
    if(Paused) { bResume=true;Accumulator=0;return; }
    if(R.Nodes.Num()!=5 || R.Couplings.Num()!=8 || M.Pelvis.ContainsNaN() || M.Thigh.ContainsNaN() || !FMath::IsFinite(Dt) || G.ContainsNaN()) { Reset();return; }
    const bool Jump=Samples && ((M.Pelvis.GetLocation()-Previous.Pelvis.GetLocation()).Size()>P.TeleportDistanceCm || M.Pelvis.GetRotation().AngularDistance(Previous.Pelvis.GetRotation())>P.TeleportAngleRadians || M.Thigh.GetRelativeTransform(M.Pelvis).GetRotation().AngularDistance(Previous.Thigh.GetRelativeTransform(Previous.Pelvis).GetRotation())>P.PoseDiscontinuityRadians);
    // A hitch beyond the entire substep budget is a controlled reset. Never
    // compress a long trajectory into the accepted span (that invents speed).
    if(Teleport || (!ShapeRebase && Jump) || Dt>P.FixedStep*P.MaxSubsteps) Reset();
    if(!Samples || bResume || ShapeRebase)
    {
        bInitializeWorldVelocity=!Samples;
        for(int32 I=0;I<5;++I)
        {
            auto& N=Nodes[I];N.RestWorld=M.Pelvis.TransformPosition(R.Nodes[I].Rest);N.PositionWorld=N.RestWorld+M.Pelvis.TransformVectorNoScale(N.Displacement);
            if(!Samples) N.VelocityWorld=M.bKnownTwist?M.PelvisVelocity+FVector::CrossProduct(M.PelvisOmega,N.PositionWorld-M.Pelvis.GetLocation()):FVector::ZeroVector;
        }
        Previous=M;PreviousRest=R;Samples=M.bKnownTwist?2:1;Accumulator=0;PreviousDt=0;bResume=false;return;
    }
    if(Dt<=0) return;
    if(!M.bKnownTwist)
    {
        M.PelvisVelocity=(M.Pelvis.GetLocation()-Previous.Pelvis.GetLocation())/Dt;M.PelvisOmega=Angular(Previous.Pelvis,M.Pelvis,Dt);
        M.ThighVelocity=(M.Thigh.GetLocation()-Previous.Thigh.GetLocation())/Dt;M.ThighOmega=Angular(Previous.Thigh,M.Thigh,Dt);
        const FVector IV=M.PelvisVelocity,IW=M.PelvisOmega,TV=M.ThighVelocity,TW=M.ThighOmega;
        // Causal endpoint estimate; world particles themselves are never rebased
        // on ordinary rest changes or frame velocity events.
        if(Samples>1 && PreviousDt>0)
        {
            const double F=Dt/(Dt+PreviousDt);
            M.PelvisVelocity+=(IV-IntervalV)*F;M.PelvisOmega+=(IW-IntervalW)*F;
            M.ThighVelocity+=(TV-ThighIntervalV)*F;M.ThighOmega+=(TW-ThighIntervalW)*F;
        }
        IntervalV=IV;IntervalW=IW;ThighIntervalV=TV;ThighIntervalW=TW;
    }
    if(Samples==1)
    {
        Previous.PelvisVelocity=M.PelvisVelocity;Previous.PelvisOmega=M.PelvisOmega;Previous.ThighVelocity=M.ThighVelocity;Previous.ThighOmega=M.ThighOmega;
        if(bInitializeWorldVelocity) for(auto& N:Nodes) N.VelocityWorld=M.PelvisVelocity+FVector::CrossProduct(M.PelvisOmega,N.PositionWorld-Previous.Pelvis.GetLocation());
        bInitializeWorldVelocity=false;
    }
    LinearVelocity=M.PelvisVelocity;LinearAcceleration=(M.PelvisVelocity-Previous.PelvisVelocity)/Dt;Omega=M.PelvisOmega;Alpha=(M.PelvisOmega-Previous.PelvisOmega)/Dt;
    const double Accepted=FMath::Min(Dt,P.FixedStep*P.MaxSubsteps),Old=Accumulator;Accumulator+=Accepted;DroppedSteps+=FMath::Max(0,FMath::FloorToInt((Dt-Accepted)/P.FixedStep));
    FVamGluteDynamicSide Rest=R;
    while(Accumulator+1.e-10>=P.FixedStep && LastSteps<P.MaxSubsteps)
    {
        const double U=FMath::Clamp((P.FixedStep*(LastSteps+1)-Old)/Accepted,0.,1.);FVamGluteMotion S;
        Interpolate(Previous.Pelvis,M.Pelvis,Previous.PelvisVelocity,M.PelvisVelocity,Dt,U,S.Pelvis,S.PelvisVelocity);
        Interpolate(Previous.Thigh,M.Thigh,Previous.ThighVelocity,M.ThighVelocity,Dt,U,S.Thigh,S.ThighVelocity);
        S.PelvisOmega=FMath::Lerp(Previous.PelvisOmega,M.PelvisOmega,U);S.ThighOmega=FMath::Lerp(Previous.ThighOmega,M.ThighOmega,U);
        for(int32 I=0;I<5;++I) { Rest.Nodes[I].Rest=FMath::Lerp(PreviousRest.Nodes[I].Rest,R.Nodes[I].Rest,U);S.RestVelocity[I]=(R.Nodes[I].Rest-PreviousRest.Nodes[I].Rest)/Dt; }
        Step(P,Rest,S,P.FixedStep,G,T);Accumulator-=P.FixedStep;++LastSteps;
    }
    Previous=M;PreviousRest=R;PreviousDt=Dt;Samples=2;LastCostMicroseconds=(FPlatformTime::Seconds()-Start)*1.e6;
}
void FVamGluteSolver::Step(const UVamGluteJiggleProfile& P,const FVamGluteDynamicSide& R,const FVamGluteMotion& M,double H,const FVector& G,const FVamGluteTuning& T)
{
    double A[15][15]={},B[15]={},X[15]={};FVector LocalX[5],Rest[5],K[5],C[5];double Mass[5];
    const FQuat Q=M.Pelvis.GetRotation();const FVector Origin=M.Pelvis.GetLocation();
    const double Support=Scale(T.Support,.1,10),Damping=Scale(T.Damping,.1,4),Mobility=Scale(T.Mobility,.25,3),Coupling=Scale(T.InternalCoupling,0,4),MassScale=Scale(T.MassScale,.1,10);
    WorldGravity=G;CurrentGravityLocal=Q.UnrotateVector(G);ReferenceGravityLocal=R.ReferenceGravityLocal;
    GravityForce=G*(R.MassKg*MassScale);
    // Schema 1 deliberately keeps the legacy cancellation. Never reinterpret old assets.
    GravityResidualLocal=P.SchemaVersion>=2?VamSecondaryGravity::ResidualLocal(G,Q,ReferenceGravityLocal):FVector::ZeroVector;
    GravityPreload=P.SchemaVersion>=2?VamSecondaryGravity::PreloadWorld(Q,ReferenceGravityLocal)*(R.MassKg*MassScale):-GravityForce;
    for(int32 I=0;I<5;++I)
    {
        const auto& D=R.Nodes[I];auto& N=Nodes[I];LocalX[I]=Q.UnrotateVector(N.PositionWorld-Origin);Rest[I]=D.Rest;const FVector Offset=LocalX[I]-Rest[I];
        const FVector PelvisPoint=M.Pelvis.TransformPosition(D.PelvisPoint),ThighPoint=M.Thigh.TransformPosition(D.ThighPointLocal);
        const FVector VP=M.PelvisVelocity+FVector::CrossProduct(M.PelvisOmega,PelvisPoint-Origin)+FVector::CrossProduct(M.PelvisOmega,N.PositionWorld-PelvisPoint);
        const FVector VT=M.ThighVelocity+FVector::CrossProduct(M.ThighOmega,ThighPoint-M.Thigh.GetLocation())+FVector::CrossProduct(M.ThighOmega,N.PositionWorld-ThighPoint);N.ThighTargetVelocity=VT;
        Mass[I]=D.MassKg*MassScale;N.Mass=Mass[I];const FVector V=Q.UnrotateVector(N.VelocityWorld),PV=Q.UnrotateVector(VP),TV=Q.UnrotateVector(VT);
        for(int32 J=0;J<3;++J)
        {
            // Evaluate travel at a common time: x_n and the prescribed rest at
            // t_(n+1) must not be compared directly for nonlinear stiffness.
            // The old-world-velocity predictor makes this Galilean invariant.
            const double PredictedOffset=Offset[J]+H*V[J];
            const double Limit=(PredictedOffset>=0?D.PositiveTravel[J]:D.NegativeTravel[J])*Mobility,Ratio=FMath::Abs(PredictedOffset)/Limit;
            const double Transition=FMath::Max(0.,(Ratio-P.SoftLimitFraction)/(1-P.SoftLimitFraction));
            K[I][J]=D.Support[J]*Support*(1+P.NonlinearGain*Ratio*Ratio+P.LimitHardening*Transition*Transition);
            // Two genuine moving dashpots. Their rest-length preloads are
            // balanced at the prescribed G0.5 rest, so fixed poses do not drift.
            const double KP=D.Support[J]*D.PelvisAttachment,KT=D.Support[J]*D.ThighAttachment;
            // Calibrate zeta at the profile's mass/support. User multipliers
            // remain independent physical controls: Mass/Support do not rewrite C.
            const double TotalDamping=2*D.DampingRatio[J]*FMath::Sqrt(D.MassKg*D.Support[J])*Damping;
            // Parallel attachments share the requested total damping. Giving
            // each branch its own full mass inflated zeta by sqrt(p)+sqrt(t).
            const double CP=P.bNormalizedAttachmentDamping?TotalDamping*D.PelvisAttachment:2*D.DampingRatio[J]*FMath::Sqrt(D.MassKg*KP)*Damping;
            const double CT=P.bNormalizedAttachmentDamping?TotalDamping*D.ThighAttachment:2*D.DampingRatio[J]*FMath::Sqrt(D.MassKg*KT)*Damping;
            C[I][J]=CP+CT;const int32 Ndx=I*3+J;
            A[Ndx][Ndx]=Mass[I]+H*C[I][J]+H*H*K[I][J];
            const FVector PLocal=Q.UnrotateVector(PelvisPoint-Origin),TLocal=Q.UnrotateVector(ThighPoint-Origin);
            const double EP=(LocalX[I][J]-PLocal[J])-(Rest[I][J]-PLocal[J]),ET=(LocalX[I][J]-TLocal[J])-(Rest[I][J]-TLocal[J]);
            const double Elastic=-K[I][J]*(D.PelvisAttachment*EP+D.ThighAttachment*ET);
            B[Ndx]=Mass[I]*V[J]+H*(Elastic+CP*PV[J]+CT*TV[J]+Mass[I]*GravityResidualLocal[J]);
            N.Travel[J]=Limit;
        }
        N.Support=D.Support*Support;N.Damping=C[I];N.NonlinearTravel=0;
    }
    for(const auto& E:R.Couplings) for(int32 J=0;J<3;++J)
    {
        const int32 U=E.A*3+J,V=E.B*3+J;const double Kij=E.Stiffness[J]*Coupling,Force=Kij*((LocalX[E.B]-Rest[E.B])-(LocalX[E.A]-Rest[E.A]))[J];
        A[U][U]+=H*H*Kij;A[V][V]+=H*H*Kij;A[U][V]-=H*H*Kij;A[V][U]-=H*H*Kij;B[U]+=H*Force;B[V]-=H*Force;
    }
    if(!VamSecondaryMath::Solve(A,B,X,15)) { Reset();return; }
    if(P.SchemaVersion>=2)
    {
        // Preserve the old interior spring exactly. Only the final half of its
        // soft-to-hard region gains a C1 barrier. Solve that force implicitly:
        // lagging a near-singular stiffness produces artificial oscillations.
        const double Start=(1+P.SoftLimitFraction)*.5,Span=1-Start;
        bool Active=false;
        for(int32 I=0;I<5;++I) for(int32 J=0;J<3;++J)
        {
            const int32 Index=3*I+J;const double Offset=LocalX[I][J]-Rest[I][J],End=Offset+H*X[Index];
            const double Limit=(End>=0?R.Nodes[I].PositiveTravel[J]:R.Nodes[I].NegativeTravel[J])*Mobility;
            Active|=FMath::Abs(End)>Start*Limit;
            X[Index]=(FMath::Clamp(End,-R.Nodes[I].NegativeTravel[J]*Mobility*.98,R.Nodes[I].PositiveTravel[J]*Mobility*.98)-Offset)/H;
        }
        if(Active) for(int32 Iteration=0;Iteration<24;++Iteration)
        {
            double Jacobian[15][15],Rhs[15],Next[15];FMemory::Memcpy(Jacobian,A,sizeof(A));FMemory::Memcpy(Rhs,B,sizeof(B));
            for(int32 I=0;I<5;++I) for(int32 J=0;J<3;++J)
            {
                const int32 Index=3*I+J;const double End=LocalX[I][J]-Rest[I][J]+H*X[Index];
                const double Limit=(End>=0?R.Nodes[I].PositiveTravel[J]:R.Nodes[I].NegativeTravel[J])*Mobility;
                const double U=FMath::Max(0.,(FMath::Abs(End)/Limit-Start)/Span),Denominator=FMath::Max(1.e-8,1-U);
                const double Coefficient=R.Nodes[I].Support[J]*Support*P.LimitHardening;
                const double Stiffness=Coefficient*U*U/Denominator;
                const double Tangent=Stiffness+FMath::Abs(End)*Coefficient*U*(2-U)/(Denominator*Denominator*Limit*Span);
                Jacobian[Index][Index]+=H*H*Tangent;
                Rhs[Index]+=H*H*Tangent*X[Index]-H*Stiffness*End;
            }
            if(!VamSecondaryMath::Solve(Jacobian,Rhs,Next,15)) {Reset();return;}
            double LineFraction=1,Change=0;
            for(int32 I=0;I<5;++I) for(int32 J=0;J<3;++J)
            {
                const int32 Index=3*I+J;const double End=LocalX[I][J]-Rest[I][J]+H*X[Index],Delta=H*(Next[Index]-X[Index]);
                const double Positive=R.Nodes[I].PositiveTravel[J]*Mobility,Negative=R.Nodes[I].NegativeTravel[J]*Mobility;
                if(Delta>0) LineFraction=FMath::Min(LineFraction,.99*(Positive-End)/Delta);
                if(Delta<0) LineFraction=FMath::Min(LineFraction,.99*(-Negative-End)/Delta);
            }
            LineFraction=FMath::Clamp(LineFraction,0.,1.);
            for(int32 Index=0;Index<15;++Index) {const double Delta=LineFraction*(Next[Index]-X[Index]);X[Index]+=Delta;Change=FMath::Max(Change,FMath::Abs(H*Delta));}
            if(Change<1.e-9) break;
        }
    }
    bSleeping=true;
    for(int32 I=0;I<5;++I)
    {
        auto& N=Nodes[I];N.VelocityWorld=Q.RotateVector(FVector(X[I*3],X[I*3+1],X[I*3+2]));N.PositionWorld+=H*N.VelocityWorld;N.RestWorld=M.Pelvis.TransformPosition(Rest[I]);N.Displacement=Q.UnrotateVector(N.PositionWorld-N.RestWorld);
        FVector Relative=Q.UnrotateVector(N.VelocityWorld-M.PelvisVelocity-FVector::CrossProduct(M.PelvisOmega,N.PositionWorld-Origin));
        for(int32 J=0;J<3;++J)
        {
            const double Positive=R.Nodes[I].PositiveTravel[J]*Mobility,Negative=R.Nodes[I].NegativeTravel[J]*Mobility;const double Bounded=FMath::Clamp(N.Displacement[J],-Negative,Positive);
            N.NonlinearTravel=FMath::Max(N.NonlinearTravel,FMath::Abs(N.Displacement[J])/(N.Displacement[J]>=0?Positive:Negative));
            if(Bounded!=N.Displacement[J]) { N.Displacement[J]=Bounded;Relative[J]=M.RestVelocity[I][J];++LimitCorrections; }
        }
        N.PositionWorld=N.RestWorld+Q.RotateVector(N.Displacement);N.RelativeVelocity=Relative-M.RestVelocity[I];
        N.VelocityWorld=M.PelvisVelocity+FVector::CrossProduct(M.PelvisOmega,N.PositionWorld-Origin)+Q.RotateVector(Relative);
        if(N.PositionWorld.ContainsNaN() || N.VelocityWorld.ContainsNaN()) { Reset();return; }
        bSleeping&=N.Displacement.Size()<P.SleepDisplacementCm && Relative.Size()<P.SleepSpeedCmS;
    }
}


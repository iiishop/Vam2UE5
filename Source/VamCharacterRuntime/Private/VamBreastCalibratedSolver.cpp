#include "VamBreastSolver.h"
#include "VamSecondaryMath.h"

namespace
{
using VamSecondaryMath::Solve;
FVector Tensor(const FVector& D,const FVector& O,const FVector& V)
{
    return FVector(D.X*V.X+O.X*V.Y+O.Y*V.Z,O.X*V.X+D.Y*V.Y+O.Z*V.Z,O.Y*V.X+O.Z*V.Y+D.Z*V.Z);
}
FVector InverseTensor(const FVector& D,const FVector& O,const FVector& V)
{
    const FVector A(D.X,O.X,O.Y),B(O.X,D.Y,O.Z),C(O.Y,O.Z,D.Z);
    const double Det=FVector::DotProduct(A,FVector::CrossProduct(B,C));
    if(FMath::Abs(Det)<1.e-14) return FVector::ZeroVector;
    return FVector(FVector::DotProduct(V,FVector::CrossProduct(B,C)),FVector::DotProduct(A,FVector::CrossProduct(V,C)),FVector::DotProduct(A,FVector::CrossProduct(B,V)))/Det;
}
double Nonlinear(double X,double Positive,double Negative,double CurvePositive,double CurveNegative,double SoftPositive,double SoftNegative)
{
    const double Limit=X>=0?Positive:Negative;
    const double Ratio=FMath::Abs(X)/FMath::Max(.0001,Limit);
    const double Soft=X>=0?SoftPositive:SoftNegative;
    const double T=FMath::Max(0.,(Ratio-Soft)/(1-Soft));
    return 1+(X>=0?CurvePositive:CurveNegative)*Ratio*Ratio+40*T*T;
}
}

void FVamBreastSolver::ProjectResidual(const FVamBreastSideProfile& R)
{
    if(Nodes.Num()!=5 || R.Nodes.Num()!=5) return;
    FVector Mean=FVector::ZeroVector,MeanV=Mean,D=Mean,O=Mean;
    double Sum=0;
    for(int32 I=0;I<5;++I) { const double M=R.Nodes[I].MassFraction;Sum+=M;Mean+=M*Nodes[I].Displacement;MeanV+=M*Nodes[I].Velocity; }
    Mean/=Sum;MeanV/=Sum;
    FVector Moment=FVector::ZeroVector,MomentV=Moment;
    for(int32 I=0;I<5;++I)
    {
        const double M=R.Nodes[I].MassFraction;const FVector L=R.Nodes[I].MassCenter-R.COM;
        Nodes[I].Displacement-=Mean;Nodes[I].Velocity-=MeanV;
        D+=M*FVector(L.Y*L.Y+L.Z*L.Z,L.X*L.X+L.Z*L.Z,L.X*L.X+L.Y*L.Y);
        O-=M*FVector(L.X*L.Y,L.X*L.Z,L.Y*L.Z);
        Moment+=M*FVector::CrossProduct(L,Nodes[I].Displacement);MomentV+=M*FVector::CrossProduct(L,Nodes[I].Velocity);
    }
    const FVector W=InverseTensor(D,O,Moment),WV=InverseTensor(D,O,MomentV);
    for(int32 I=0;I<5;++I) { const FVector L=R.Nodes[I].MassCenter-R.COM;Nodes[I].Displacement-=FVector::CrossProduct(W,L);Nodes[I].Velocity-=FVector::CrossProduct(WV,L); }
}
FVector FVamBreastSolver::NodeOffset(const FVamBreastSideProfile& R,int32 I) const
{
    return COMDisplacement+FVector::CrossProduct(AngularDisplacement,R.Nodes[I].Rest-R.COM)+(Nodes.IsValidIndex(I)?Nodes[I].Displacement:FVector::ZeroVector);
}
FVector FVamBreastSolver::NodeRelativeVelocity(const FVamBreastSideProfile& R,int32 I) const
{
    return COMVelocity+FVector::CrossProduct(RelativeAngularVelocity,R.Nodes[I].MassCenter-R.COM)+(Nodes.IsValidIndex(I)?Nodes[I].Velocity:FVector::ZeroVector);
}
void FVamBreastSolver::ApplyFrameVelocityChange(const FVamBreastSideProfile& R,const FVector& DV,const FVector& DW)
{
    Nodes.SetNum(5);
    COMVelocity-=DV+FVector::CrossProduct(DW,R.COM+COMDisplacement);
    RelativeAngularVelocity-=DW;
    // Transport the deformation's velocity as well. Decompose this correction once;
    // the reconstructed nodal world velocity is unchanged by an instantaneous twist change.
    FVector D=FVector::ZeroVector,O=D,Moment=D,Mean=D,Correction[5];
    for(int32 I=0;I<5;++I)
    {
        const FVector L=R.Nodes[I].MassCenter-R.COM;const double M=R.Nodes[I].MassFraction;
        Correction[I]=-FVector::CrossProduct(DW,FVector::CrossProduct(AngularDisplacement,L)+Nodes[I].Displacement);
        Mean+=M*Correction[I];Moment+=M*FVector::CrossProduct(L,Correction[I]);
        D+=M*FVector(L.Y*L.Y+L.Z*L.Z,L.X*L.X+L.Z*L.Z,L.X*L.X+L.Y*L.Y);O-=M*FVector(L.X*L.Y,L.X*L.Z,L.Y*L.Z);
    }
    const FVector Rotation=InverseTensor(D,O,Moment);COMVelocity+=Mean;RelativeAngularVelocity+=Rotation;
    for(int32 I=0;I<5;++I) Nodes[I].Velocity+=Correction[I]-Mean-FVector::CrossProduct(Rotation,R.Nodes[I].MassCenter-R.COM);
}
void FVamBreastSolver::StepCalibrated(const UVamBreastJiggleProfile& P,const FVamBreastSideProfile& R,double H,const FVector& G)
{
    if(R.Nodes.Num()!=5 || H<=0 || R.MassKg<=0) return;
    Nodes.SetNum(5);ProjectResidual(R);
    ApplyFrameVelocityChange(R,H*LinearAcceleration,H*AngularAcceleration);
    const auto Before=Nodes;const FVector BeforeCOM=COMVelocity;
    double Mass[5];FVector Lever[5],Force[5];
    FVector PointD=FVector::ZeroVector,PointO=PointD,Torque=PointD,TotalForce=PointD,ResidualCoriolisTorque=PointD,RigidCoriolisTorque=PointD;
    CentrifugalLoad=FVector::ZeroVector;
    for(int32 I=0;I<5;++I)
    {
        Mass[I]=R.MassKg*R.Nodes[I].MassFraction;Lever[I]=R.Nodes[I].MassCenter-R.COM;
        const auto& L=Lever[I];
        PointD+=Mass[I]*FVector(L.Y*L.Y+L.Z*L.Z,L.X*L.X+L.Z*L.Z,L.X*L.X+L.Y*L.Y);
        PointO-=Mass[I]*FVector(L.X*L.Y,L.X*L.Z,L.Y*L.Z);
        const FVector Position=R.Nodes[I].MassCenter+COMDisplacement+FVector::CrossProduct(AngularDisplacement,L)+Nodes[I].Displacement;
        const FVector Centrifugal=-Mass[I]*FVector::CrossProduct(AngularVelocity,FVector::CrossProduct(AngularVelocity,Position));
        const FVector RigidCoriolis=-2*Mass[I]*FVector::CrossProduct(AngularVelocity,FVector::CrossProduct(RelativeAngularVelocity,L));
        Force[I]=Mass[I]*(G-R.ImportedGravityLocal)+Centrifugal+RigidCoriolis;
        RigidCoriolisTorque+=FVector::CrossProduct(L,RigidCoriolis);
        ResidualCoriolisTorque-=2*Mass[I]*FVector::CrossProduct(L,FVector::CrossProduct(AngularVelocity,Nodes[I].Velocity));
        CentrifugalLoad+=Centrifugal;TotalForce+=Force[I];Torque+=FVector::CrossProduct(L,Force[I]);
    }
    const FVector RotationLoad=InverseTensor(PointD,PointO,Torque);
    ResidualLoadMagnitude=0;
    for(int32 I=0;I<5;++I) { Force[I]-=R.Nodes[I].MassFraction*TotalForce+Mass[I]*FVector::CrossProduct(RotationLoad,Lever[I]);ResidualLoadMagnitude+=Force[I].SizeSquared(); }
    ResidualLoadMagnitude=FMath::Sqrt(ResidualLoadMagnitude);TranslationLoadMagnitude=TotalForce.Size();RotationLoadMagnitude=Torque.Size();
    // COM has the sum of calibrated anchor supports, with its own mass and travel.
    double CA[15][15]={},CB[15]={},CV[15]={};
    for(int32 Axis=0;Axis<3;++Axis)
    {
        double K=0;
        for(const auto& N:R.Nodes) K+=N.SupportStiffness[Axis]*Nonlinear(COMDisplacement[Axis],R.COMPositiveLimit[Axis],R.COMNegativeLimit[Axis],N.PositiveNonlinearity[Axis],N.NegativeNonlinearity[Axis],P.SoftLimitFraction,N.NegativeSoftFraction[Axis]);
        const double D=2*R.COMDampingRatio[Axis]*FMath::Sqrt(R.MassKg*K);
        CA[Axis][Axis]=R.MassKg+H*D+H*H*K;
        CB[Axis]=R.MassKg*COMVelocity[Axis]+H*(TotalForce[Axis]-K*COMDisplacement[Axis]);
        for(int32 J=0;J<3;++J) { FVector U=FVector::ZeroVector;U[J]=1;CA[Axis][J]+=2*H*R.MassKg*FVector::CrossProduct(AngularVelocity,U)[Axis]; }
    }
    if(!Solve(CA,CB,CV,3)) { Reset();return; }
    COMVelocity=FVector(CV[0],CV[1],CV[2]);COMDisplacement+=H*COMVelocity;
    for(int32 Axis=0;Axis<3;++Axis)
    {
        const double X=FMath::Clamp(COMDisplacement[Axis],-R.COMNegativeLimit[Axis],R.COMPositiveLimit[Axis]);
        if(X!=COMDisplacement[Axis]) { COMDisplacement[Axis]=X;COMVelocity[Axis]=0;++LimitCorrections; }
    }
    double A[15][15]={},B[15]={},V[15]={};
    for(int32 I=0;I<5;++I)
    {
        const auto& N=R.Nodes[I];
        for(int32 Axis=0;Axis<3;++Axis)
        {
            const int32 Row=I*3+Axis;
            const double K=N.SupportStiffness[Axis]*Nonlinear(Nodes[I].Displacement[Axis],N.PositiveLimitCm[Axis],N.NegativeLimitCm[Axis],N.PositiveNonlinearity[Axis],N.NegativeNonlinearity[Axis],P.SoftLimitFraction,N.NegativeSoftFraction[Axis]);
            const double D=2*N.DampingRatio[Axis]*FMath::Sqrt(Mass[I]*K);
            A[Row][Row]=Mass[I]+H*D+H*H*K;
            B[Row]=Mass[I]*Nodes[I].Velocity[Axis]+H*(Force[I][Axis]-K*Nodes[I].Displacement[Axis]);
            for(int32 Column=0;Column<3;++Column) { FVector Unit=FVector::ZeroVector;Unit[Column]=1;A[Row][I*3+Column]+=2*H*Mass[I]*FVector::CrossProduct(AngularVelocity,Unit)[Axis]; }
        }
    }
    for(const auto& E:R.Couplings) for(int32 Axis=0;Axis<3;++Axis)
    {
        const int32 I=E.A*3+Axis,J=E.B*3+Axis;const double K=E.Stiffness[Axis];
        A[I][I]+=H*H*K;A[J][J]+=H*H*K;A[I][J]-=H*H*K;A[J][I]-=H*H*K;
        const double F=K*(Nodes[E.B].Displacement[Axis]-Nodes[E.A].Displacement[Axis]);B[I]+=H*F;B[J]-=H*F;
    }
    if(!Solve(A,B,V,15)) { Reset();return; }
    for(int32 I=0;I<5;++I) Nodes[I].Velocity=FVector(V[I*3],V[I*3+1],V[I*3+2]);
    ProjectResidual(R);
    for(auto& N:Nodes) N.Displacement+=H*N.Velocity;
    // A common emergency radial projection preserves both modal null spaces.
    double Scale=1;
    for(int32 I=0;I<5;++I) for(int32 Axis=0;Axis<3;++Axis)
    {
        const double X=Nodes[I].Displacement[Axis];const double L=X>=0?R.Nodes[I].PositiveLimitCm[Axis]:R.Nodes[I].NegativeLimitCm[Axis];
        if(FMath::Abs(X)>L) Scale=FMath::Min(Scale,L/FMath::Abs(X));
    }
    if(Scale<1) { ++LimitCorrections;for(auto& N:Nodes) { N.Displacement*=Scale;N.Velocity*=Scale; } }
    const auto Inertia=[&](const FVector& W){return Tensor(R.InertiaDiagonal,R.InertiaOffDiagonal,W);};
    // Full inertia includes each node's intrinsic volume moment. Replace the nodal
    // rigid centrifugal torque with this complete tensor; residual keeps tidal load only.
    const FVector ExternalTorque=Torque-RigidCoriolisTorque-FVector::CrossProduct(AngularVelocity,Inertia(AngularVelocity)-Tensor(PointD,PointO,AngularVelocity))+ResidualCoriolisTorque;
    double RA[15][15]={},RB[15]={},RV[15]={};const FVector Momentum=Inertia(RelativeAngularVelocity);
    for(int32 Axis=0;Axis<3;++Axis)
    {
        const double K=R.RotationalStiffness[Axis]*Nonlinear(AngularDisplacement[Axis],R.AngularLimitRadians[Axis],R.AngularLimitRadians[Axis],1.,1.,.65,.65);
        const double D=2*R.RotationalDampingRatio[Axis]*FMath::Sqrt(R.InertiaDiagonal[Axis]*K);
        RB[Axis]=Momentum[Axis]+H*(ExternalTorque[Axis]-K*AngularDisplacement[Axis]);
        for(int32 J=0;J<3;++J)
        {
            FVector Unit=FVector::ZeroVector;Unit[J]=1;
            RA[Axis][J]=Inertia(Unit)[Axis]+H*(FVector::CrossProduct(AngularVelocity,Inertia(Unit))+FVector::CrossProduct(Unit,Inertia(AngularVelocity)))[Axis];
        }
        RA[Axis][Axis]+=H*D+H*H*K;
    }
    if(!Solve(RA,RB,RV,3)) { Reset();return; }
    RelativeAngularVelocity=FVector(RV[0],RV[1],RV[2]);AngularDisplacement+=H*RelativeAngularVelocity;
    for(int32 Axis=0;Axis<3;++Axis)
    {
        const double X=FMath::Clamp(AngularDisplacement[Axis],-R.AngularLimitRadians[Axis],R.AngularLimitRadians[Axis]);
        if(X!=AngularDisplacement[Axis]) { AngularDisplacement[Axis]=X;RelativeAngularVelocity[Axis]=0;++LimitCorrections; }
    }
    double Speed=COMVelocity.Size(),Acceleration=(COMVelocity-BeforeCOM).Size()/H;
    for(int32 I=0;I<5;++I)
    {
        if(Nodes[I].Velocity.ContainsNaN() || Nodes[I].Displacement.ContainsNaN()) { Reset();return; }
        Speed=FMath::Max(Speed,Nodes[I].Velocity.Size());Acceleration=FMath::Max(Acceleration,(Nodes[I].Velocity-Before[I].Velocity).Size()/H);
    }
    if(COMDisplacement.ContainsNaN() || COMVelocity.ContainsNaN() || AngularDisplacement.ContainsNaN() || RelativeAngularVelocity.ContainsNaN()) { Reset();return; }
    bSleeping=Speed<P.SleepSpeedCmS && Acceleration<P.SleepAccelerationCmS2 && RelativeAngularVelocity.Size()<.00001;
}

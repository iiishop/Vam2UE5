#include "VamBreastSolver.h"

namespace
{
// Tiny dense solve for the five-node implicit network (15 unknowns), with pivoting.
bool Solve(double A[15][15],double B[15],double X[15],int32 Count)
{
    for(int32 K=0;K<Count;++K)
    {
        int32 Pivot=K;for(int32 I=K+1;I<Count;++I) if(FMath::Abs(A[I][K])>FMath::Abs(A[Pivot][K])) Pivot=I;
        if(!FMath::IsFinite(A[Pivot][K]) || FMath::Abs(A[Pivot][K])<1.e-12) return false;
        if(Pivot!=K) { for(int32 J=K;J<Count;++J) Swap(A[K][J],A[Pivot][J]);Swap(B[K],B[Pivot]); }
        for(int32 I=K+1;I<Count;++I)
        {
            const double F=A[I][K]/A[K][K];
            for(int32 J=K;J<Count;++J) A[I][J]-=F*A[K][J];B[I]-=F*B[K];
        }
    }
    for(int32 I=Count-1;I>=0;--I) { double V=B[I];for(int32 J=I+1;J<Count;++J) V-=A[I][J]*X[J];X[I]=V/A[I][I];if(!FMath::IsFinite(X[I])) return false; }
    return true;
}
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

void FVamBreastSolver::StepCalibrated(const UVamBreastJiggleProfile& P,const FVamBreastSideProfile& R,double H,const FVector& G)
{
    if(R.Nodes.Num()!=5 || H<=0 || R.MassKg<=0) return;
    Nodes.SetNum(5);const auto Before=Nodes;
    double Mass[5];FVector Lever[5],Force[5];
    FVector PointD=FVector::ZeroVector,PointO=FVector::ZeroVector,Torque=FVector::ZeroVector;
    for(int32 I=0;I<5;++I)
    {
        Mass[I]=R.MassKg*R.Nodes[I].MassFraction;Lever[I]=R.Nodes[I].MassCenter-R.COM;
        const auto& L=Lever[I];
        PointD+=Mass[I]*FVector(L.Y*L.Y+L.Z*L.Z,L.X*L.X+L.Z*L.Z,L.X*L.X+L.Y*L.Y);
        PointO-=Mass[I]*FVector(L.X*L.Y,L.X*L.Z,L.Y*L.Z);
        const FVector Position=R.Nodes[I].MassCenter+Nodes[I].Displacement;
        Force[I]=Mass[I]*(G-R.ImportedGravityLocal-LinearAcceleration-FVector::CrossProduct(AngularAcceleration,Position)-FVector::CrossProduct(AngularVelocity,FVector::CrossProduct(AngularVelocity,Position)));
        Torque+=FVector::CrossProduct(L,Force[I]);
    }
    // Split off the rigid angular component of the nodal load: do not apply rotational inertia twice.
    const FVector RotationLoad=InverseTensor(PointD,PointO,Torque);
    for(int32 I=0;I<5;++I) Force[I]-=Mass[I]*FVector::CrossProduct(RotationLoad,Lever[I]);
    // Limit projection can introduce a rigid component into the residual coordinates.
    // Re-establish the modal gauge, otherwise that component cannot return to rest.
    FVector PositionMoment=FVector::ZeroVector;
    for(int32 I=0;I<5;++I) PositionMoment+=Mass[I]*FVector::CrossProduct(Lever[I],Nodes[I].Displacement);
    const FVector RigidPosition=InverseTensor(PointD,PointO,PositionMoment);
    for(int32 I=0;I<5;++I) Nodes[I].Displacement-=FVector::CrossProduct(RigidPosition,Lever[I]);
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
            for(int32 Column=0;Column<3;++Column)
            {
                FVector Unit=FVector::ZeroVector;Unit[Column]=1;
                A[Row][I*3+Column]+=2*H*Mass[I]*FVector::CrossProduct(AngularVelocity,Unit)[Axis];
            }
        }
    }
    // Implicit symmetric graph Laplacian: stable even with stronger internal connections.
    for(const auto& E:R.Couplings) for(int32 Axis=0;Axis<3;++Axis)
    {
        const int32 I=E.A*3+Axis,J=E.B*3+Axis;const double K=E.Stiffness[Axis];
        A[I][I]+=H*H*K;A[J][J]+=H*H*K;A[I][J]-=H*H*K;A[J][I]-=H*H*K;
        const double F=K*(Nodes[E.B].Displacement[Axis]-Nodes[E.A].Displacement[Axis]);B[I]+=H*F;B[J]-=H*F;
    }
    if(!Solve(A,B,V,15)) { Reset();return; }
    FVector AngularMomentum=FVector::ZeroVector;
    for(int32 I=0;I<5;++I) { Nodes[I].Velocity=FVector(V[I*3],V[I*3+1],V[I*3+2]);AngularMomentum+=Mass[I]*FVector::CrossProduct(Lever[I],Nodes[I].Velocity); }
    const FVector RigidVelocity=InverseTensor(PointD,PointO,AngularMomentum);
    for(int32 I=0;I<5;++I)
    {
        // Mass-orthogonal deformation modes have no duplicate rigid angular motion.
        Nodes[I].Velocity-=FVector::CrossProduct(RigidVelocity,Lever[I]);Nodes[I].Displacement+=H*Nodes[I].Velocity;
        for(int32 Axis=0;Axis<3;++Axis)
        {
            const double X=FMath::Clamp(Nodes[I].Displacement[Axis],-R.Nodes[I].NegativeLimitCm[Axis],R.Nodes[I].PositiveLimitCm[Axis]);
            if(X!=Nodes[I].Displacement[Axis]) { ++LimitCorrections;Nodes[I].Displacement[Axis]=X;Nodes[I].Velocity[Axis]=0; }
        }
    }
    // Three independent angular states about the calibrated COM. Full symmetric inertia tensor.
    const auto Inertia=[&](const FVector& W){return Tensor(R.InertiaDiagonal,R.InertiaOffDiagonal,W);};
    const FVector ExternalTorque=-Inertia(AngularAcceleration)-FVector::CrossProduct(AngularVelocity,Inertia(AngularVelocity));
    double RA[15][15]={},RB[15]={},RV[15]={};
    const FVector Momentum=Inertia(RelativeAngularVelocity);
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
    double Speed=0,Acceleration=0;
    for(int32 I=0;I<5;++I)
    {
        if(Nodes[I].Velocity.ContainsNaN() || Nodes[I].Displacement.ContainsNaN()) { Reset();return; }
        Speed=FMath::Max(Speed,Nodes[I].Velocity.Size());Acceleration=FMath::Max(Acceleration,(Nodes[I].Velocity-Before[I].Velocity).Size()/H);
    }
    if(AngularDisplacement.ContainsNaN() || RelativeAngularVelocity.ContainsNaN()) { Reset();return; }
    bSleeping=Speed<P.SleepSpeedCmS && Acceleration<P.SleepAccelerationCmS2 && RelativeAngularVelocity.Size()<.00001;
    // Sleep is diagnostic only; never discard sub-threshold angular or translation momentum.
}

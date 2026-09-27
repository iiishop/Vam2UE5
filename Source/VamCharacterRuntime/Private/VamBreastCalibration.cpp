#include "VamBreastCalibration.h"

void VamBreastCalibration::CalibrateCOM(FVamBreastSideProfile& R)
{
    R.COMSupport=R.COMDampingRatio=R.COMPositiveLimit=R.COMNegativeLimit=FVector::ZeroVector;
    for(const auto& N:R.Nodes)
    {
        R.COMSupport+=N.SupportStiffness;
        R.COMDampingRatio+=N.DampingRatio*N.MassFraction;
        R.COMPositiveLimit+=N.PositiveLimitCm*N.MassFraction;
        R.COMNegativeLimit+=N.NegativeLimitCm*N.MassFraction;
    }
}

void VamBreastCalibration::Calibrate(FVamBreastSideProfile& R,double Density,double Modulus)
{
    R.MassKg=R.EffectiveVolumeCm3*Density;R.ReferenceMassKg=R.MassKg;
    double Sum=0;for(const auto& N:R.Nodes) Sum+=FMath::Max(1.e-8,N.EffectiveVolumeCm3);
    R.COM=FVector::ZeroVector;
    for(auto& N:R.Nodes)
    {
        N.MassFraction=FMath::Max(1.e-8,N.EffectiveVolumeCm3)/Sum;
        N.EffectiveVolumeCm3=R.EffectiveVolumeCm3*N.MassFraction;
        R.COM+=N.MassCenter*N.MassFraction;
    }
    R.InertiaDiagonal=R.InertiaOffDiagonal=FVector::ZeroVector;
    R.RotationalStiffness=FVector::ZeroVector;
    const double Depth=FMath::Max(.1,R.EffectiveDepthCm);
    const double Width=FMath::Max(.1,R.SizeCm.Y),Height=FMath::Max(.1,R.SizeCm.Z);
    for(auto& N:R.Nodes)
    {
        const double M=R.MassKg*N.MassFraction;
        const FVector Lever=N.MassCenter-R.COM;
        // Each semantic volume has a finite local spherical moment, preventing point-mass singularities.
        const double Radius=FMath::Pow(3*N.EffectiveVolumeCm3/(4*PI),1./3.);
        const double Intrinsic=.4*M*Radius*Radius;
        R.InertiaDiagonal+=M*FVector(Lever.Y*Lever.Y+Lever.Z*Lever.Z,Lever.X*Lever.X+Lever.Z*Lever.Z,Lever.X*Lever.X+Lever.Y*Lever.Y)+FVector(Intrinsic);
        R.InertiaOffDiagonal-=M*FVector(Lever.X*Lever.Y,Lever.X*Lever.Z,Lever.Y*Lever.Z);
        N.LeverArmCm=N.MassCenter.Size();
        const double RootDistance=FMath::Max(Depth*.25,N.LeverArmCm);
        const double Upper=FMath::Clamp((N.Rest.Z-R.COM.Z)/Height,-.5,.5);
        const double Semantic=N.Semantic==TEXT("Upper")?1.5:N.Semantic==TEXT("Medial")?1.35:N.Semantic==TEXT("Lower")?.7:N.Semantic==TEXT("Lateral")?.8:1.;
        // E*A/L: Pa * cm^2/cm * .01 -> kg/s^2. Longer COM lever reduces attachment stiffness.
        const double K=Modulus*.01*R.SupportAreaCm2*N.MassFraction/RootDistance/(1+RootDistance/FMath::Max(.1,FMath::Sqrt(R.SupportAreaCm2)))*Semantic*(1+Upper*.3);
        N.SupportStiffness=K*FVector(1.,1.+R.RootSizeCm.Y/Width*.35,.85+R.RootSizeCm.Z/Height*.2);
        for(int32 A=0;A<3;++A) N.FrequencyHz[A]=FMath::Sqrt(N.SupportStiffness[A]/M)/(2*PI);
        N.DampingRatio=FVector(.24,.28,.25)*(1+.3*R.SupportAreaCm2/(R.SupportAreaCm2+RootDistance*RootDistance));
        const double Freedom=1/FMath::Sqrt(Semantic);
        N.PositiveLimitCm=Freedom*FVector(Depth*.22,FMath::Min(Width*.24,R.RootSizeCm.Y*.4),Height*.22);
        const double Hanging=FMath::Max(0.,R.COM.Z-N.Rest.Z);
        N.NegativeLimitCm=Freedom*FVector(Depth*.09,N.PositiveLimitCm.Y/Freedom,Height*.18+Hanging*.25);
        const double Aspect=FMath::Clamp(Depth/Width,.25,4.);
        N.PositiveNonlinearity=FVector(.5+.4*Aspect,.7,.6);
        N.NegativeNonlinearity=FVector(2.+Aspect,.7,.8);
        const FVector L=N.Rest-R.COM;
        R.RotationalStiffness+=FVector(N.SupportStiffness.Y*L.Z*L.Z+N.SupportStiffness.Z*L.Y*L.Y,N.SupportStiffness.X*L.Z*L.Z+N.SupportStiffness.Z*L.X*L.X,N.SupportStiffness.X*L.Y*L.Y+N.SupportStiffness.Y*L.X*L.X);
    }
    // Torsion of the finite support patch supplements node lever stiffness.
    const double Torsion=Modulus*.01*R.SupportAreaCm2*(R.RootSizeCm.Y*R.RootSizeCm.Y+R.RootSizeCm.Z*R.RootSizeCm.Z)/(48*Depth);
    R.RotationalStiffness+=FVector(Torsion,Torsion*.7,Torsion*.7);
    R.AngularLimitRadians=FVector(FMath::Atan2(FMath::Min(Width,Height)*.22,Depth),FMath::Atan2(Height*.22,Depth),FMath::Atan2(Width*.24,Depth));
    for(int32 A=0;A<3;++A) R.AngularLimitRadians[A]=FMath::Clamp(R.AngularLimitRadians[A],.04,.45);
    R.Couplings.Reset();
    const int32 Pairs[8][2]={{0,1},{0,2},{0,3},{0,4},{1,3},{1,4},{2,3},{2,4}};
    for(int32 I=0;I<8;++I)
    {
        FVamBreastCoupling E;E.A=Pairs[I][0];E.B=Pairs[I][1];
        const auto& A=R.Nodes[E.A];const auto& B=R.Nodes[E.B];
        const double Length=FMath::Max(Depth*.15,(A.MassCenter-B.MassCenter).Size());
        const double Area=FMath::Pow(FMath::Min(A.EffectiveVolumeCm3,B.EffectiveVolumeCm3),2./3.);
        E.Stiffness=FVector(Modulus*.01*Area/Length*(I<4?.12:.035));R.Couplings.Add(E);
    }
    CalibrateCOM(R);
}

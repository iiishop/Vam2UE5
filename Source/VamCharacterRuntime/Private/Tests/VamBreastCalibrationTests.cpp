#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "VamBreastSolver.h"
#include "VamBreastCalibration.h"

namespace
{
FVamBreastSideProfile Fixture(double Scale=1)
{
    FVamBreastSideProfile R;R.EffectiveVolumeCm3=400*Scale*Scale*Scale;R.EffectiveDepthCm=8*Scale;R.SupportAreaCm2=70*Scale*Scale;
    R.SizeCm=FVector(8,10,12)*Scale;R.RootSizeCm=FVector(1,9,10)*Scale;
    const FVector Positions[]={FVector(7,0,0),FVector(5,0,3),FVector(8,0,-4),FVector(5,-3,0),FVector(8,4,0)};
    const TCHAR* Names[]={TEXT("Core"),TEXT("Upper"),TEXT("Lower"),TEXT("Medial"),TEXT("Lateral")};
    for(int32 I=0;I<5;++I) { FVamBreastNodeParameters N;N.Semantic=Names[I];N.Rest=Positions[I]*Scale;N.MassCenter=N.Rest*.75;N.EffectiveVolumeCm3=(40+I*20)*Scale*Scale*Scale;R.Nodes.Add(N); }
    VamBreastCalibration::Calibrate(R,.00102,1200);return R;
}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVamBreastCalibrationTest,"Vam.Breast.Calibration",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FVamBreastCalibrationTest::RunTest(const FString& Parameters)
{
    auto* P=NewObject<UVamBreastJiggleProfile>();P->SchemaVersion=2;
    const auto R=Fixture();const auto Large=Fixture(1.5);const double H=1./120.;
    TestTrue(TEXT("Larger volume increases mass and rotational inertia"),Large.MassKg>R.MassKg*3 && Large.InertiaDiagonal.X>R.InertiaDiagonal.X*5);
    TestTrue(TEXT("Frequency is geometry dependent"),!Large.Nodes[0].FrequencyHz.Equals(R.Nodes[0].FrequencyHz,.01));
    TestEqual(TEXT("Sparse graph has eight edges"),R.Couplings.Num(),8);
    TestTrue(TEXT("Node fractions reflect measured volume"),R.Nodes[0].MassFraction<R.Nodes[4].MassFraction);
    double Sum=0;for(const auto& N:R.Nodes) Sum+=N.MassFraction;TestTrue(TEXT("Node fractions sum to one"),FMath::Abs(Sum-1)<1.e-10);
    FVamBreastTuning T;auto E=T.DynamicsRest(R);TestTrue(TEXT("Identity tuning preserves calibration"),E.Nodes[0].SupportStiffness.Equals(R.Nodes[0].SupportStiffness,0));
    T.Support=2;E=T.DynamicsRest(R);
    TestTrue(TEXT("Support changes only anchor stiffness"),E.Nodes[0].SupportStiffness.Equals(R.Nodes[0].SupportStiffness*2,0) && E.Nodes[0].PositiveLimitCm.Equals(R.Nodes[0].PositiveLimitCm,0) && E.MassKg==R.MassKg && E.Nodes[0].DampingRatio==R.Nodes[0].DampingRatio && E.Couplings[0].Stiffness==R.Couplings[0].Stiffness);
    T=FVamBreastTuning();T.Mobility=2;E=T.DynamicsRest(R);
    TestTrue(TEXT("Mobility does not change small-signal frequency or mass"),E.Nodes[0].SupportStiffness==R.Nodes[0].SupportStiffness && E.MassKg==R.MassKg && E.Nodes[0].PositiveLimitCm==R.Nodes[0].PositiveLimitCm*2);
    T=FVamBreastTuning();T.InternalCoupling=2;E=T.DynamicsRest(R);
    TestTrue(TEXT("Coupling does not modify anchor stiffness"),E.Nodes[0].SupportStiffness==R.Nodes[0].SupportStiffness && E.Couplings[0].Stiffness==R.Couplings[0].Stiffness*2 && E.RotationalStiffness==R.RotationalStiffness);
    T=FVamBreastTuning();T.MassScale=2;E=T.DynamicsRest(R);
    TestTrue(TEXT("Mass Scale only changes mass and tensor"),E.MassKg==R.MassKg*2 && E.InertiaDiagonal==R.InertiaDiagonal*2 && E.InertiaOffDiagonal==R.InertiaOffDiagonal*2 && E.Nodes[0].SupportStiffness==R.Nodes[0].SupportStiffness && E.Nodes[0].DampingRatio==R.Nodes[0].DampingRatio && E.Nodes[0].PositiveLimitCm==R.Nodes[0].PositiveLimitCm && E.Couplings[0].Stiffness==R.Couplings[0].Stiffness && E.RotationalStiffness==R.RotationalStiffness);
    T=FVamBreastTuning();T.Damping=2;E=T.DynamicsRest(R);
    TestTrue(TEXT("Damping only changes ratios"),E.Nodes[0].DampingRatio==R.Nodes[0].DampingRatio*2 && E.MassKg==R.MassKg && E.Nodes[0].SupportStiffness==R.Nodes[0].SupportStiffness);
    FVamBreastSolver Low,High;Low.Nodes.SetNum(5);High.Nodes.SetNum(5);
    for(int32 I=0;I<5;++I) Low.Nodes[I].Displacement=High.Nodes[I].Displacement=FVector(.1,0,0);
    double LowTail=0,HighTail=0;
    for(int32 I=0;I<180;++I) { Low.Step(*P,R,H,FVector::ZeroVector);High.Step(*P,R,H,FVector::ZeroVector,T);if(I>120) {LowTail+=Low.Nodes[0].Displacement.SizeSquared();HighTail+=High.Nodes[0].Displacement.SizeSquared();} }
    TestTrue(TEXT("Higher undercritical damping reduces ringing tail"),HighTail<LowTail);
    FVamBreastSolver S;
    for(int32 I=0;I<1200;++I) S.Advance(*P,R,FTransform(FVector(I*H*100,0,0)),H,FVector::ZeroVector,false,false);
    TestTrue(TEXT("Calibrated constant velocity has no persistent lag"),S.Nodes[0].Displacement.Size()<.001);
    S.LinearAcceleration=FVector(100,0,0);for(int32 I=0;I<120;++I) S.Step(*P,R,H,FVector::ZeroVector);
    TestTrue(TEXT("Calibrated positive acceleration produces negative lag"),S.Nodes[0].Displacement.X<-.01);
    S.LinearAcceleration=FVector(-100,0,0);for(int32 I=0;I<120;++I) S.Step(*P,R,H,FVector::ZeroVector);
    TestTrue(TEXT("Calibrated braking continues forward"),S.Nodes[0].Displacement.X>.01);
    S.LinearAcceleration=FVector::ZeroVector;for(int32 I=0;I<1200;++I) S.Step(*P,R,H,FVector::ZeroVector);
    TestTrue(TEXT("Calibrated braking recovers"),S.Nodes[0].Displacement.Size()<.001);
    S.Reset();S.AngularAcceleration=FVector(0,0,2);for(int32 I=0;I<120;++I) S.Step(*P,R,H,FVector::ZeroVector);
    TestTrue(TEXT("True angular state lags positive angular acceleration"),S.AngularDisplacement.Z<-.001 && S.Nodes[0].Displacement.Y<-.001);
    S.AngularAcceleration=FVector(0,0,-2);for(int32 I=0;I<120;++I) S.Step(*P,R,H,FVector::ZeroVector);
    TestTrue(TEXT("Angular braking preserves forward rotation"),S.AngularDisplacement.Z>.001);
    S.Reset();S.AngularVelocity=FVector(0,0,2);for(int32 I=0;I<1200;++I) S.Step(*P,R,H,FVector::ZeroVector);
    TestTrue(TEXT("Constant omega produces radial response"),S.Nodes[0].Displacement.X>.01);
    S.Nodes[0].Velocity=FVector(2,0,0);S.Step(*P,R,H,FVector::ZeroVector);
    TestTrue(TEXT("Moving nodes experience Coriolis"),FMath::Abs(S.Nodes[0].Velocity.Y)>.0001);
    const auto Frozen=S;S.Advance(*P,R,FTransform::Identity,H,FVector::ZeroVector,false,true);
    TestTrue(TEXT("Pause freezes angular and translation state"),S.AngularDisplacement==Frozen.AngularDisplacement && S.RelativeAngularVelocity==Frozen.RelativeAngularVelocity && S.Nodes[0].Displacement==Frozen.Nodes[0].Displacement);
    S.Advance(*P,R,FTransform(FVector(10000,0,0)),H,FVector::ZeroVector,true,false);
    TestTrue(TEXT("Teleport clears angular momentum"),S.RelativeAngularVelocity.IsNearlyZero() && S.Nodes[0].Velocity.IsNearlyZero());
    FVamBreastSolver Peer;Peer.Step(*P,R,H,FVector::ZeroVector);S.LinearAcceleration=FVector(100);S.Step(*P,R,H,FVector::ZeroVector);
    TestTrue(TEXT("Instance isolation"),Peer.Nodes[0].Displacement.IsNearlyZero() && Peer.AngularDisplacement.IsNearlyZero());
    TArray<TArray<FVector>> Tracks,Angles;
    for(int32 FPS:{30,60,120})
    {
        FVamBreastSolver Instance;TArray<FVector> Track,Angle;
        for(int32 Frame=0;Frame<FPS*8;++Frame)
        {
            const double Time=double(Frame)/FPS;
            Instance.Advance(*P,R,FTransform(FQuat(FVector::UpVector,.5*FMath::Sin(Time*2)),FVector(20*FMath::Sin(Time*3),0,0)),1./FPS,FVector::ZeroVector,false,false);
            if(Frame%(FPS/30)==0 && !Instance.Nodes.IsEmpty()) {Track.Add(Instance.Nodes[0].Displacement);Angle.Add(Instance.AngularDisplacement);}
        }
        Tracks.Add(Track);Angles.Add(Angle);
    }
    double Max30=0,Max60=0,MaxAngular=0;
    for(int32 I=0;I<Tracks[0].Num();++I) {Max30=FMath::Max(Max30,(Tracks[0][I]-Tracks[2][I]).Size());Max60=FMath::Max(Max60,(Tracks[1][I]-Tracks[2][I]).Size());MaxAngular=FMath::Max(MaxAngular,(Angles[0][I]-Angles[2][I]).Size());}
    TestTrue(TEXT("Calibrated full trajectory 30/120 < .3 cm"),Max30<.3);TestTrue(TEXT("Calibrated full trajectory 60/120 < .2 cm"),Max60<.2);TestTrue(TEXT("Angular 30/120 < .03 radians"),MaxAngular<.03);
    AddInfo(FString::Printf(TEXT("Calibrated divergence 30/120 %.6f cm 60/120 %.6f cm angular %.6f rad"),Max30,Max60,MaxAngular));
    S.Reset();bool Finite=true;
    T=FVamBreastTuning();T.Support=.1;T.Damping=.1;T.Mobility=3;T.InternalCoupling=4;T.MassScale=10;
    for(int32 I=0;I<12000;++I)
    {
        S.AngularVelocity=FVector(1,2,10);S.AngularAcceleration=FVector(FMath::Sin(I*H)*5);S.Step(*P,R,H,FVector(0,980,980),T);
        for(const auto& N:S.Nodes) Finite=Finite && !N.Displacement.ContainsNaN() && !N.Velocity.ContainsNaN() && N.Velocity.Size()<1000;
        Finite=Finite && !S.AngularDisplacement.ContainsNaN() && S.AngularDisplacement.Size()<2 && S.RelativeAngularVelocity.Size()<100;
    }
    TestTrue(TEXT("Extreme tuning long-run angular and nodal state bounded"),Finite);
    S.LinearAcceleration=S.AngularVelocity=S.AngularAcceleration=FVector::ZeroVector;
    for(int32 I=0;I<4800;++I) S.Step(*P,R,H,FVector::ZeroVector);
    TestTrue(TEXT("Unforced calibrated state decays"),S.Nodes[0].Displacement.Size()<.01 && S.AngularDisplacement.Size()<.001);
    return true;
}
#endif

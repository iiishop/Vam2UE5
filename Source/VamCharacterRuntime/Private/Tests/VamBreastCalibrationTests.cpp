#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "VamBreastSolver.h"
#include "VamBreastCalibration.h"
#include "VamBreastDebugTrajectory.h"

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
    Low.COMDisplacement=High.COMDisplacement=FVector(.1,0,0);
    double LowTail=0,HighTail=0;
    for(int32 I=0;I<180;++I) { Low.Step(*P,R,H,FVector::ZeroVector);High.Step(*P,R,H,FVector::ZeroVector,T);if(I>120) {LowTail+=Low.COMDisplacement.SizeSquared();HighTail+=High.COMDisplacement.SizeSquared();} }
    TestTrue(TEXT("Higher undercritical damping reduces ringing tail"),HighTail<LowTail);
    FVamBreastSolver S;
    for(int32 I=0;I<1200;++I) S.Advance(*P,R,FTransform(FVector(I*H*100,0,0)),H,FVector::ZeroVector,false,false);
    TestTrue(TEXT("Calibrated constant velocity has no persistent lag"),S.NodeOffset(R,0).Size()<.001);
    S.LinearAcceleration=FVector(100,0,0);for(int32 I=0;I<120;++I) S.Step(*P,R,H,FVector::ZeroVector);
    TestTrue(TEXT("Calibrated positive acceleration produces negative lag"),S.NodeOffset(R,0).X<-.01);
    S.LinearAcceleration=FVector(-100,0,0);for(int32 I=0;I<120;++I) S.Step(*P,R,H,FVector::ZeroVector);
    TestTrue(TEXT("Calibrated braking continues forward"),S.NodeOffset(R,0).X>.01);
    S.LinearAcceleration=FVector::ZeroVector;for(int32 I=0;I<1200;++I) S.Step(*P,R,H,FVector::ZeroVector);
    TestTrue(TEXT("Calibrated braking recovers"),S.NodeOffset(R,0).Size()<.001);
    S.Reset();S.AngularAcceleration=FVector(0,0,2);for(int32 I=0;I<120;++I) S.Step(*P,R,H,FVector::ZeroVector);
    TestTrue(TEXT("True angular state lags positive angular acceleration"),S.AngularDisplacement.Z<-.001 && S.NodeOffset(R,0).Y<-.001);
    S.AngularAcceleration=FVector(0,0,-2);for(int32 I=0;I<120;++I) S.Step(*P,R,H,FVector::ZeroVector);
    TestTrue(TEXT("Angular braking preserves forward rotation"),S.AngularDisplacement.Z>.001);
    S.Reset();S.AngularVelocity=FVector(0,0,2);for(int32 I=0;I<1200;++I) S.Step(*P,R,H,FVector::ZeroVector);
    TestTrue(TEXT("Constant omega produces radial response"),S.NodeOffset(R,0).X>.01);
    S.Nodes[0].Velocity=FVector(2,0,0);S.Step(*P,R,H,FVector::ZeroVector);
    TestTrue(TEXT("Moving nodes experience Coriolis"),FMath::Abs(S.Nodes[0].Velocity.Y)>.0001);
    const auto Frozen=S;S.Advance(*P,R,FTransform::Identity,H,FVector::ZeroVector,false,true);
    TestTrue(TEXT("Pause freezes angular and translation state"),S.AngularDisplacement==Frozen.AngularDisplacement && S.RelativeAngularVelocity==Frozen.RelativeAngularVelocity && S.NodeOffset(R,0)==Frozen.NodeOffset(R,0));
    S.Advance(*P,R,FTransform(FVector(10000,0,0)),H,FVector::ZeroVector,true,false);
    TestTrue(TEXT("Teleport clears angular momentum"),S.RelativeAngularVelocity.IsNearlyZero() && S.Nodes[0].Velocity.IsNearlyZero());
    FVamBreastSolver Peer;Peer.Step(*P,R,H,FVector::ZeroVector);S.LinearAcceleration=FVector(100);S.Step(*P,R,H,FVector::ZeroVector);
    TestTrue(TEXT("Instance isolation"),Peer.NodeOffset(R,0).IsNearlyZero() && Peer.AngularDisplacement.IsNearlyZero());
    TArray<TArray<FVector>> Tracks,Angles;
    for(int32 FPS:{30,60,120})
    {
        FVamBreastSolver Instance;TArray<FVector> Track,Angle;
        for(int32 Frame=0;Frame<FPS*8;++Frame)
        {
            const double Time=double(Frame)/FPS;
            Instance.Advance(*P,R,FTransform(FQuat(FVector::UpVector,.5*FMath::Sin(Time*2)),FVector(20*FMath::Sin(Time*3),0,0)),1./FPS,FVector::ZeroVector,false,false);
            if(Frame%(FPS/30)==0 && !Instance.Nodes.IsEmpty()) {Track.Add(Instance.NodeOffset(R,0));Angle.Add(Instance.AngularDisplacement);}
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
    TestTrue(TEXT("Unforced calibrated state decays"),S.NodeOffset(R,0).Size()<.01 && S.AngularDisplacement.Size()<.001);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVamBreastModalTest,"Vam.Breast.MovingFrameModal",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FVamBreastModalTest::RunTest(const FString& Parameters)
{
    auto* P=NewObject<UVamBreastJiggleProfile>();P->SchemaVersion=3;const auto R=Fixture();const double H=1./120.;
    FVamBreastSolver S;S.Nodes.SetNum(5);S.COMDisplacement=FVector(.2,-.1,.3);S.COMVelocity=FVector(3,5,-1);
    S.AngularDisplacement=FVector(.03,-.02,.01);S.RelativeAngularVelocity=FVector(.1,.3,-.1);
    for(int32 I=0;I<5;++I) {S.Nodes[I].Displacement=FVector(.01*I,.005*I*I,-.003*I);S.Nodes[I].Velocity=FVector(.03*I,.02*I,-.01*I*I);}
    S.ProjectResidual(R);const auto Before=S;
    const FVector V0(20,-5,2),W0(.4,-.2,.6),DV(-30,10,4),DW(.3,-.5,1.1);
    S.ApplyFrameVelocityChange(R,DV,DW);double Error=0;
    for(int32 I=0;I<5;++I)
    {
        const FVector Lever=R.Nodes[I].MassCenter-R.COM;
        const FVector Position=R.Nodes[I].MassCenter+S.COMDisplacement+FVector::CrossProduct(S.AngularDisplacement,Lever)+S.Nodes[I].Displacement;
        const FVector OldV=V0+FVector::CrossProduct(W0,Position)+Before.NodeRelativeVelocity(R,I);
        const FVector NewV=V0+DV+FVector::CrossProduct(W0+DW,Position)+S.NodeRelativeVelocity(R,I);
        Error=FMath::Max(Error,(OldV-NewV).Size());
    }
    TestTrue(TEXT("Hard linear/angular twist change preserves each nodal world velocity"),Error<1.e-10);
    S.Reset();S.ApplyFrameVelocityChange(R,FVector(30,0,0),FVector(0,0,1));
    TestTrue(TEXT("Linear impulse applied exactly once"),(S.COMVelocity+FVector(30,0,0)+FVector::CrossProduct(FVector(0,0,1),R.COM)).Size()<1.e-10);
    TestTrue(TEXT("Angular impulse applied exactly once"),(S.RelativeAngularVelocity+FVector(0,0,1)).Size()<1.e-10);
    // Validate the production accumulator path, not only the explicit kick API.
    FVamMovingFrameSample F;F.bHasTwist=true;S.Reset();S.AdvanceFrame(*P,R,F,H,FVector::ZeroVector,false,false);
    auto* Tiny=NewObject<UVamBreastJiggleProfile>();Tiny->SchemaVersion=3;Tiny->FixedStep=1.e-6;
    F.VelocityWorld=FVector(30,0,0);F.OmegaWorld=FVector(0,0,1);
    S.AdvanceFrame(*Tiny,R,F,1.e-6,FVector::ZeroVector,false,false);
    TestTrue(TEXT("AdvanceFrame does not add acceleration a second time"),(S.COMVelocity+F.VelocityWorld+FVector::CrossProduct(F.OmegaWorld,R.COM)).Size()<.01 && (S.RelativeAngularVelocity+F.OmegaWorld).Size()<.001);
    FVamBreastTuning Stress;Stress.Support=.1;Stress.Damping=.1;Stress.Mobility=3;
    S.Reset();double MaxMean=0,MaxMoment=0,Residual=0;
    for(int32 Step=0;Step<7200;++Step)
    {
        S.AngularVelocity=FVector(0,0,2);S.Step(*P,R,H,FVector::ZeroVector,Stress);
        FVector Mean=FVector::ZeroVector,Moment=Mean;double Residual2=0;
        for(int32 I=0;I<5;++I)
        {
            const double M=R.Nodes[I].MassFraction;Mean+=M*S.Nodes[I].Displacement;
            Moment+=M*FVector::CrossProduct(R.Nodes[I].MassCenter-R.COM,S.Nodes[I].Displacement);Residual2+=M*S.Nodes[I].Displacement.SizeSquared();
        }
        MaxMean=FMath::Max(MaxMean,Mean.Size());MaxMoment=FMath::Max(MaxMoment,Moment.Size());Residual=FMath::Sqrt(Residual2);
    }
    TestTrue(TEXT("Residual zero translation throughout long rotation"),MaxMean<1.e-8);
    TestTrue(TEXT("Residual zero angular mode throughout long rotation"),MaxMoment<1.e-8);
    TestTrue(TEXT("Constant rotation radial COM dominates residual"),S.COMDisplacement.X>0 && S.COMDisplacement.Size()>Residual*2);
    AddInfo(FString::Printf(TEXT("Momentum error %.12g; modal mean %.12g moment %.12g; steady COM %.6f residual RMS %.6f cm"),Error,MaxMean,MaxMoment,S.COMDisplacement.Size(),Residual));
    S.Reset();S.LinearAcceleration=FVector(150,0,0);for(int32 I=0;I<120;++I) S.Step(*P,R,H,FVector::ZeroVector);
    double MaxResidual=0;for(const auto& N:S.Nodes) MaxResidual=FMath::Max(MaxResidual,N.Displacement.Size());
    TestTrue(TEXT("Uniform acceleration enters COM only"),S.COMDisplacement.X<0 && MaxResidual<1.e-8);
    FVamMotionRamp Ramp;Ramp.Start(150,30,-10,0,.4);double X,V,A,J;
    Ramp.Evaluate(0,X,V,A,J);TestTrue(TEXT("Smooth stop preserves initial derivatives"),FMath::Abs(V-150)<1.e-8 && FMath::Abs(A-30)<1.e-8 && FMath::Abs(J+10)<1.e-8);
    Ramp.Evaluate(.4,X,V,A,J);TestTrue(TEXT("Smooth stop ends at rest with zero acceleration and jerk"),FMath::Abs(V)+FMath::Abs(A)+FMath::Abs(J)<1.e-8);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVamBreastJumpTest,"Vam.Breast.SmoothJump",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FVamBreastJumpTest::RunTest(const FString& Parameters)
{
    FVamJumpTrajectory Jump;double X,V,A,J;
    Jump.Evaluate(Jump.Duration(),X,V,A,J);
    TestTrue(TEXT("Landing returns to ground at rest"),FMath::Abs(X)+FMath::Abs(V)+FMath::Abs(A)+FMath::Abs(J)<1.e-6);
    const double Apex=Jump.Takeoff+Jump.LaunchSpeed/Jump.Gravity;
    Jump.Evaluate(Apex,X,V,A,J);TestTrue(TEXT("Ballistic apex has zero velocity, unchanged gravity and jerk"),FMath::Abs(V)<1.e-8 && FMath::Abs(A+Jump.Gravity)<1.e-8 && FMath::Abs(J)<1.e-8);
    for(double Boundary:{Jump.Takeoff,Jump.Takeoff+Jump.FlightTime(),Jump.Duration()})
    {
        double X0,V0,A0,J0;Jump.Evaluate(Boundary-1.e-7,X0,V0,A0,J0);Jump.Evaluate(Boundary+1.e-7,X,V,A,J);
        TestTrue(TEXT("Jump phase boundaries continuous through acceleration"),FMath::Abs(X-X0)<.001 && FMath::Abs(V-V0)<.001 && FMath::Abs(A-A0)<.01 && FMath::Abs(J-J0)<.2);
    }
    auto* P=NewObject<UVamBreastJiggleProfile>();P->SchemaVersion=3;auto R=Fixture();R.ImportedGravityLocal=FVector(0,0,-980);
    for(bool KnownTwist:{true,false})
    {
    TArray<TArray<FVector>> Tracks;double LatestApex=0,BodyApex=Apex,MaxHeight=-1.e20,MinTakeoff=0,LandingTail=0;
    FVamBreastTuning T;T.Support=.1;T.Damping=.1;T.MassScale=3;
    for(int32 FPS:{30,60,120})
    {
        FVamBreastSolver S;TArray<FVector> Track;
        for(int32 Frame=0;Frame<=FPS*5;++Frame)
        {
            const double Time=double(Frame)/FPS;Jump.Evaluate(Time,X,V,A,J);
            FVamMovingFrameSample F;F.Transform=FTransform(FVector(0,0,X));F.bHasTwist=KnownTwist;F.VelocityWorld=FVector(0,0,V);
            S.AdvanceFrame(*P,R,F,1./FPS,FVector(0,0,-980),false,false,T);
            if(Frame%(FPS/30)==0) Track.Add(S.COMDisplacement);
            if(FPS==120)
            {
                if(Time<Jump.Takeoff) MinTakeoff=FMath::Min(MinTakeoff,S.COMDisplacement.Z);
                if(Time<Jump.Takeoff+Jump.FlightTime() && X+S.COMDisplacement.Z>MaxHeight) {MaxHeight=X+S.COMDisplacement.Z;LatestApex=Time;}
                if(Time>Jump.Duration() && Time<Jump.Duration()+1) LandingTail=FMath::Max(LandingTail,S.COMVelocity.Size());
            }
        }
        Tracks.Add(Track);
    }
    double E30=0,E60=0;for(int32 I=0;I<Tracks[0].Num();++I) {E30=FMath::Max(E30,(Tracks[0][I]-Tracks[2][I]).Size());E60=FMath::Max(E60,(Tracks[1][I]-Tracks[2][I]).Size());}
    TestTrue(TEXT("Smooth takeoff downward lag"),MinTakeoff<-.01);
    // Stress trajectory apex timing is recorded, not used to infer sub-step phase lag.
    TestTrue(TEXT("Landing carries state into ringing"),LandingTail>.01);
    TestTrue(TEXT("Jump 30/120 bounded within .3 cm"),E30<.3);TestTrue(TEXT("Jump 60/120 bounded within .2 cm"),E60<.2);
    AddInfo(FString::Printf(TEXT("Jump known twist %d body apex %.6f tissue apex %.6f; takeoff lag %.6f; landing tail %.6f; FPS error %.6f / %.6f cm"),KnownTwist,BodyApex,LatestApex,MinTakeoff,LandingTail,E30,E60));
    }
    FVamBreastSolver Phase;FVamBreastTuning PhaseT;PhaseT.Damping=.1;PhaseT.Mobility=3;
    double BodyMax=-1.e20,TissueMax=-1.e20,BodyTime=0,TissueTime=0,TissueVelocityAtBodyTop=0;
    for(int32 Frame=0;Frame<120*(Jump.Takeoff+Jump.FlightTime());++Frame)
    {
        const double Time=Frame/120.;Jump.Evaluate(Time,X,V,A,J);
        FVamMovingFrameSample F;F.Transform=FTransform(FVector(0,0,X));F.bHasTwist=true;F.VelocityWorld=FVector(0,0,V);
        Phase.AdvanceFrame(*P,R,F,1./120.,FVector(0,0,-980),false,false,PhaseT);
        if(X>BodyMax) {BodyMax=X;BodyTime=Time;TissueVelocityAtBodyTop=V+Phase.COMVelocity.Z;}
        if(X+Phase.COMDisplacement.Z>TissueMax) {TissueMax=X+Phase.COMDisplacement.Z;TissueTime=Time;}
    }
    TestTrue(TEXT("Tissue apex later than sampled body apex by more than one full step"),TissueTime>BodyTime+1./120.);
    TestTrue(TEXT("Tissue world velocity remains upward at body's highest sampled position"),TissueVelocityAtBodyTop>1.);
    TestEqual(TEXT("Delayed apex example does not rely on hard limit clipping"),Phase.LimitCorrections,0);
    AddInfo(FString::Printf(TEXT("Resolved jump phase body %.6f tissue %.6f delay %.6f s tissue velocity at body top %.6f cm/s hard limits %d"),BodyTime,TissueTime,TissueTime-BodyTime,TissueVelocityAtBodyTop,Phase.LimitCorrections));
    return true;
}
#endif

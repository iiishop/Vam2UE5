#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "VamGluteSolver.h"
#include "VamBreastDebugTrajectory.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

namespace
{
// Synthetic mechanics fixture; asset tests separately use measured character data.
FVamGluteDynamicSide MechanicsFixture()
{
    FVamGluteDynamicSide R;R.MassKg=1;R.Dimensions=FVector(15,20,24);
    const FVector Positions[]={FVector(9,0,0),FVector(8,0,4),FVector(9,0,-4),FVector(7,-4,0),FVector(9,4,0)};
    const double Attachment[]={.75,.95,.55,.97,.6};
    for(int32 I=0;I<5;++I)
    {
        FVamGluteDynamicNode N;N.BoneIndex=100+I;N.Rest=Positions[I];N.MassKg=.2;
        N.PelvisPoint=FVector(0,Positions[I].Y,Positions[I].Z);N.ThighPointLocal=FVector(0,0,-8);
        N.PelvisAttachment=Attachment[I];N.ThighAttachment=1-Attachment[I];N.Support=FVector(I==1||I==3?160:50);
        N.PositiveTravel=N.NegativeTravel=FVector(2);R.Nodes.Add(N);
    }
    const int32 Graph[][2]={{0,1},{0,2},{0,3},{0,4},{1,3},{1,4},{2,3},{2,4}};
    for(const auto& P:Graph) { FVamGluteDynamicEdge E;E.A=P[0];E.B=P[1];E.Stiffness=FVector(2);R.Couplings.Add(E); }
    return R;
}
FVamGluteMotion LinearMotion(double Position,double Velocity)
{
    FVamGluteMotion M;M.Pelvis=M.Thigh=FTransform(FVector(Position,0,0));M.PelvisVelocity=M.ThighVelocity=FVector(Velocity,0,0);M.bKnownTwist=true;return M;
}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVamGluteDynamicsTest,"Vam.Glute.G1.Mechanics",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FVamGluteDynamicsTest::RunTest(const FString& Parameters)
{
    auto* P=NewObject<UVamGluteJiggleProfile>();P->bNormalizedAttachmentDamping=true;const auto R=MechanicsFixture();FVamGluteTuning T;
    const double H=P->FixedStep;const FVector G(0,0,-980);FVamGluteSolver S;
    for(int32 I=0;I<=1200;++I) S.Advance(*P,R,LinearMotion(I*H*100,100),H,G,T);
    TestTrue(TEXT("Constant linear velocity no persistent lag"),S.Nodes[0].Displacement.Size()<1.e-6);
    S.Reset();for(int32 I=0;I<=240;++I) { const double Time=I*H;S.Advance(*P,R,LinearMotion(50*Time*Time,100*Time),H,G,T); }
    TestTrue(TEXT("Acceleration lags opposite direction"),S.Nodes[0].Displacement.X<-.01);
    for(int32 I=1;I<=120;++I) { const double Time=I*H;S.Advance(*P,R,LinearMotion(200+200*Time-100*Time*Time,200-200*Time),H,G,T); }
    TestTrue(TEXT("Braking continues forward"),S.Nodes[0].Displacement.X>.01);
    for(int32 I=0;I<1200;++I) S.Advance(*P,R,LinearMotion(300,0),H,G,T);
    TestTrue(TEXT("Settles to current loaded structural rest"),S.Nodes[0].Displacement.Size()<1.e-6);
    TestTrue(TEXT("Gravity preload balances fixed-pose gravity"),(S.GravityForce+S.GravityPreload).IsNearlyZero());
    S.Reset();
    for(int32 I=0;I<=1200;++I)
    {
        FVamGluteMotion M;M.bKnownTwist=true;M.Pelvis=M.Thigh=FTransform(FQuat(FVector::UpVector,2*I*H));M.PelvisOmega=M.ThighOmega=FVector(0,0,2);S.Advance(*P,R,M,H,G,T);
    }
    TestTrue(TEXT("Constant angular velocity has outward centrifugal response"),S.Nodes[0].Displacement.X>.01);
    S.Reset();for(int32 I=0;I<=120;++I)
    {
        const double Time=I*H;FVamGluteMotion M;M.bKnownTwist=true;M.Pelvis=M.Thigh=FTransform(FQuat(FVector::UpVector,Time*Time));M.PelvisOmega=M.ThighOmega=FVector(0,0,2*Time);S.Advance(*P,R,M,H,G,T);
    }
    TestTrue(TEXT("Angular acceleration produces tangential lag"),S.Nodes[0].Displacement.Y<-.001);
    for(const FVector Axis:{FVector::XAxisVector,FVector::YAxisVector,FVector::ZAxisVector})
    {
        FVamGluteSolver Rotating;
        for(int32 I=0;I<=600;++I) {FVamGluteMotion M;M.bKnownTwist=true;M.Pelvis=M.Thigh=FTransform(FQuat(Axis,I*H));M.PelvisOmega=M.ThighOmega=Axis;Rotating.Advance(*P,R,M,H,G,T);}
        const FVector Radial=R.Nodes[2].Rest-Axis*FVector::DotProduct(Axis,R.Nodes[2].Rest);
        TestTrue(TEXT("Yaw pitch roll all produce outward inertial response"),FVector::DotProduct(Rotating.Nodes[2].Displacement,Radial)>.001);
    }
    const auto Frozen=S;S.Advance(*P,R,LinearMotion(0,0),H,G,T,false,true);
    TestTrue(TEXT("Pause preserves positions and velocities"),S.Nodes[0].PositionWorld==Frozen.Nodes[0].PositionWorld && S.Nodes[0].VelocityWorld==Frozen.Nodes[0].VelocityWorld);
    S.Advance(*P,R,LinearMotion(10000,0),H,G,T,true);
    TestTrue(TEXT("Teleport clears invalid motion history"),S.Nodes[0].Displacement.IsNearlyZero() && S.Nodes[0].VelocityWorld.IsNearlyZero());
    S.Reset();double Peaks[5]={};
    for(int32 I=0;I<2400;++I)
    {
        const double Time=I*H;FVamGluteMotion M;M.bKnownTwist=true;M.Thigh=FTransform(FQuat(FVector::YAxisVector,.3*FMath::Sin(Time*4)));
        M.ThighOmega=FVector(0,1.2*FMath::Cos(Time*4),0);S.Advance(*P,R,M,H,G,T);
        for(int32 N=0;N<5;++N) Peaks[N]=FMath::Max(Peaks[N],S.Nodes[N].Displacement.Size());
    }
    TestTrue(TEXT("Moving femur drives lower/lateral more than upper/medial"),Peaks[2]>.01 && Peaks[4]>.01 && Peaks[2]>Peaks[1] && Peaks[4]>Peaks[3]);
    AddInfo(FString::Printf(TEXT("Thigh-only peaks cm core %.6f upper %.6f lower %.6f medial %.6f lateral %.6f"),Peaks[0],Peaks[1],Peaks[2],Peaks[3],Peaks[4]));
    const FVector Velocity=S.Nodes[0].VelocityWorld;auto Shape=R;Shape.Nodes[0].Rest+=FVector(.1,0,0);
    S.Advance(*P,Shape,S.Previous,H,G,T,false,false,true);
    TestTrue(TEXT("Small Shape rebase preserves world velocity"),S.Nodes[0].VelocityWorld.Equals(Velocity,0));
    FVamGluteSolver Peer;Peer.Advance(*P,R,LinearMotion(0,0),H,G,T);
    TestTrue(TEXT("Two instances share calibration without mutable state"),Peer.Nodes[0].Displacement.IsNearlyZero() && !S.Nodes[0].Displacement.IsNearlyZero());
    // No time passes: changing the prescribed rest must not move a world particle.
    const FVector Position=S.Nodes[0].PositionWorld;S.Advance(*P,Shape,S.Previous,0,G,T);
    TestTrue(TEXT("Ordinary rest update does not reinterpret world momentum"),S.Nodes[0].PositionWorld==Position && S.Nodes[0].VelocityWorld==Velocity);
    FVamGluteSolver Rebased;FVamGluteMotion Unknown;Rebased.Advance(*P,R,Unknown,H,G,T);Rebased.Advance(*P,R,Unknown,H,G,T);
    Rebased.Nodes[0].VelocityWorld=FVector(10,0,0);Rebased.Advance(*P,R,Unknown,H,G,T,false,false,true);Rebased.Advance(*P,R,Unknown,H,G,T);
    TestTrue(TEXT("Shape rebase momentum also survives the next history-prime frame"),Rebased.Nodes[0].VelocityWorld.X>1);
    TArray<uint8> Bytes;FMemoryWriter Writer(Bytes);auto Original=R;FVamGluteDynamicSide::StaticStruct()->SerializeItem(Writer,&Original,nullptr);
    FVamGluteDynamicSide Loaded;FMemoryReader Reader(Bytes);FVamGluteDynamicSide::StaticStruct()->SerializeItem(Reader,&Loaded,nullptr);
    TestTrue(TEXT("Calibration serialization preserves masses and attachments"),Loaded.Nodes.Num()==5 && Loaded.Nodes[2].MassKg==R.Nodes[2].MassKg && Loaded.Nodes[2].ThighAttachment==R.Nodes[2].ThighAttachment);
    FVamGluteSolver Base,Heavy,Supported,Mobile,Damped;auto Still=LinearMotion(0,0);
    Base.Advance(*P,R,Still,H,G,T);Base.Advance(*P,R,Still,H,G,T);
    Heavy=Supported=Mobile=Damped=Base;
    auto Tune=T;Tune.MassScale=2;Heavy.Step(*P,R,Still,H,G,Tune);
    TestTrue(TEXT("Mass scale only changes mass; support/damper/travel remain"),Heavy.Nodes[0].Mass==2*Base.Nodes[0].Mass && Heavy.Nodes[0].Support==Base.Nodes[0].Support && Heavy.Nodes[0].Damping==Base.Nodes[0].Damping && Heavy.Nodes[0].Travel==Base.Nodes[0].Travel);
    Tune=T;Tune.Support=2;Supported.Step(*P,R,Still,H,G,Tune);
    TestTrue(TEXT("Support scales K independently"),Supported.Nodes[0].Support==2*Base.Nodes[0].Support && Supported.Nodes[0].Damping==Base.Nodes[0].Damping && Supported.Nodes[0].Mass==Base.Nodes[0].Mass);
    Tune=T;Tune.Mobility=2;Mobile.Step(*P,R,Still,H,G,Tune);
    TestTrue(TEXT("Mobility scales travel independently"),Mobile.Nodes[0].Travel==2*Base.Nodes[0].Travel && Mobile.Nodes[0].Support==Base.Nodes[0].Support && Mobile.Nodes[0].Damping==Base.Nodes[0].Damping);
    Tune=T;Tune.Damping=2;Damped.Step(*P,R,Still,H,G,Tune);
    TestTrue(TEXT("Damping scales C independently"),Damped.Nodes[0].Damping==2*Base.Nodes[0].Damping && Damped.Nodes[0].Support==Base.Nodes[0].Support && Damped.Nodes[0].Travel==Base.Nodes[0].Travel);
    S.Reset();for(int32 I=0;I<120;++I) S.Advance(*P,R,LinearMotion(I*H*100,100),H,G,T);
    S.Advance(*P,R,LinearMotion(100,0),H,G,T);TestTrue(TEXT("Hard stop preserves forward momentum with bounded lag"),S.Nodes[0].VelocityWorld.X>0 && S.Nodes[0].Displacement.Size()<2);
    FVamJumpTrajectory Jump;S.Reset();double JumpPeak=0,LandingPeak=0;bool Finite=true;
    for(int32 I=0;I<1200;++I)
    {
        const double Time=I*H;double X,V,A,J;Jump.Evaluate(Time,X,V,A,J);FVamGluteMotion M;M.bKnownTwist=true;M.Pelvis=M.Thigh=FTransform(FVector(0,0,X));M.PelvisVelocity=M.ThighVelocity=FVector(0,0,V);
        S.Advance(*P,R,M,H,G,T);const double D=S.Nodes[0].Displacement.Size();JumpPeak=FMath::Max(JumpPeak,D);if(Time>Jump.Takeoff+Jump.FlightTime() && Time<Jump.Duration()+.5) LandingPeak=FMath::Max(LandingPeak,D);Finite&=FMath::IsFinite(D)&&D<=2.000001;
    }
    TestTrue(TEXT("Continuous jump/landing has bounded response then decays"),Finite && JumpPeak>.01 && LandingPeak>.01 && S.Nodes[0].Displacement.Size()<.001);
    AddInfo(FString::Printf(TEXT("Jump peak %.6f landing peak %.6f tail %.9f cm"),JumpPeak,LandingPeak,S.Nodes[0].Displacement.Size()));
    FVamGluteSolver Slow,Boosted;double BoostError=0;
    for(int32 I=0;I<1200;++I)
    {
        const double Time=I*H,X=20*FMath::Sin(3*Time),V=60*FMath::Cos(3*Time);
        Slow.Advance(*P,R,LinearMotion(X,V),H,G,T);Boosted.Advance(*P,R,LinearMotion(X+300*Time,V+300),H,G,T);
        for(int32 N=0;N<5;++N) BoostError=FMath::Max(BoostError,(Slow.Nodes[N].Displacement-Boosted.Nodes[N].Displacement).Size());
    }
    TestTrue(TEXT("Nonlinear dynamics invariant under constant world velocity boost"),BoostError<1.e-7);
    AddInfo(FString::Printf(TEXT("Galilean boost 300 cm/s max residual difference %.12f cm"),BoostError));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVamGluteRateTest,"Vam.Glute.G1.FrameRateStability",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FVamGluteRateTest::RunTest(const FString& Parameters)
{
    auto* P=NewObject<UVamGluteJiggleProfile>();P->bNormalizedAttachmentDamping=true;const auto R=MechanicsFixture();FVamGluteTuning T;TArray<TArray<FVector>> Tracks;TArray<double> Peaks,Phases,Settling;
    for(int32 FPS:{30,60,120})
    {
        FVamGluteSolver S;TArray<FVector> Track;
        for(int32 I=0;I<=FPS*12;++I)
        {
            const double Time=double(I)/FPS;FVamGluteMotion M;
            M.Pelvis=FTransform(FQuat(FVector::UpVector,.5*FMath::Sin(2*Time)),FVector(20*FMath::Sin(3*Time),0,5*FMath::Sin(4*Time)));
            M.Thigh=FTransform(FQuat(FVector::YAxisVector,.3*FMath::Sin(4*Time)))*M.Pelvis;
            S.Advance(*P,R,M,1./FPS,FVector(0,0,-980),T);
            if(I%(FPS/30)==0) Track.Add(S.Nodes[0].Displacement);
        }
        double Peak=0,Real=0,Imag=0;
        for(int32 I=0;I<Track.Num();++I) {Peak=FMath::Max(Peak,Track[I].Size());if(I>=120) {Real+=Track[I].X*FMath::Cos(3*I/30.);Imag+=Track[I].X*FMath::Sin(3*I/30.);}}
        Peaks.Add(Peak);Phases.Add(FMath::Atan2(Imag,Real)/3);Tracks.Add(Track);
        auto Still=S.Previous;Still.bKnownTwist=true;Still.PelvisVelocity=Still.PelvisOmega=Still.ThighVelocity=Still.ThighOmega=FVector::ZeroVector;double LastAbove=0;
        for(int32 I=1;I<=FPS*10;++I) {S.Advance(*P,R,Still,1./FPS,FVector(0,0,-980),T);if(S.Nodes[0].Displacement.Size()>.001 || S.Nodes[0].RelativeVelocity.Size()>.005) LastAbove=double(I)/FPS;}
        Settling.Add(LastAbove);TestTrue(TEXT("Each frame rate settles after stopping"),S.Nodes[0].Displacement.Size()<1.e-6);
        AddInfo(FString::Printf(TEXT("G1 %d FPS peak %.6f cm spectral phase %.6f s settle %.6f s"),FPS,Peak,Phases.Last(),LastAbove));
    }
    double Max30=0,Max60=0;for(int32 I=0;I<Tracks[0].Num();++I) { Max30=FMath::Max(Max30,(Tracks[0][I]-Tracks[2][I]).Size());Max60=FMath::Max(Max60,(Tracks[1][I]-Tracks[2][I]).Size()); }
    AddInfo(FString::Printf(TEXT("G1 trajectory divergence 30/120 %.9f cm 60/120 %.9f cm"),Max30,Max60));
    TestTrue(TEXT("30/120 full trajectory divergence below .3 cm"),Max30<.3);TestTrue(TEXT("60/120 full trajectory divergence below .2 cm"),Max60<.2);
    TestTrue(TEXT("Peak differences below .1 cm"),FMath::Abs(Peaks[0]-Peaks[2])<.1 && FMath::Abs(Peaks[1]-Peaks[2])<.1);
    TestTrue(TEXT("Resolved periodic phase differs below .05 s"),FMath::Abs(Phases[0]-Phases[2])<.05 && FMath::Abs(Phases[1]-Phases[2])<.05);
    TestTrue(TEXT("Settling differs below .15 s"),FMath::Abs(Settling[0]-Settling[2])<.15 && FMath::Abs(Settling[1]-Settling[2])<.15);
    FVamGluteSolver S;bool Finite=true;double Cost=0;
    T.Support=.1;T.Damping=.1;T.Mobility=3;T.InternalCoupling=4;T.MassScale=10;
    for(int32 I=0;I<12000;++I)
    {
        const double Time=I/120.;auto M=LinearMotion(30*FMath::Sin(Time*8),240*FMath::Cos(Time*8));
        S.Advance(*P,R,M,1./120.,FVector(0,0,-980),T);Cost+=S.LastCostMicroseconds;
        for(const auto& N:S.Nodes) Finite&=!N.PositionWorld.ContainsNaN() && !N.VelocityWorld.ContainsNaN() && N.Displacement.GetAbsMax()<=6.00001 && N.VelocityWorld.Size()<2000;
    }
    TestTrue(TEXT("Extreme tuning 100 seconds no NaN/explosion"),Finite);
    AddInfo(FString::Printf(TEXT("G1 five-node advance mean %.3f us (12000 calls; Development CPU measurement)"),Cost/12000));
    // At minimum damping and maximum mass, a fixed 100 s is not a fixed
    // settling criterion. Use twenty measured linear-envelope time constants.
    double SlowestDecay=MAX_dbl;
    for(const auto& N:S.Nodes) SlowestDecay=FMath::Min(SlowestDecay,N.Damping.GetMin()/(2*N.Mass));
    const int32 SettleFrames=FMath::CeilToInt(20/FMath::Max(1.e-6,SlowestDecay)*120);
    AddInfo(FString::Printf(TEXT("Extreme tuning settle envelope %.6f s^-1; horizon %.3f s"),SlowestDecay,SettleFrames/120.));
    for(int32 I=0;I<SettleFrames;++I) S.Advance(*P,R,LinearMotion(S.Previous.Pelvis.GetLocation().X,0),1./120.,FVector(0,0,-980),T);
    TestTrue(TEXT("Long-run unforced energy decays"),S.Nodes[0].Displacement.Size()<.001 && S.Nodes[0].RelativeVelocity.Size()<.001);
    T.Support=10;T.Damping=4;T.Mobility=.25;T.MassScale=.1;Finite=true;S.Reset();
    for(int32 I=0;I<12000;++I) {const double Time=I/120.;S.Advance(*P,R,LinearMotion(10*FMath::Sin(8*Time),80*FMath::Cos(8*Time)),1./120.,FVector(0,0,-980),T);for(const auto& N:S.Nodes) Finite&=!N.VelocityWorld.ContainsNaN() && N.Displacement.GetAbsMax()<=.500001;}
    TestTrue(TEXT("High support/high damping/small mass 100 seconds bounded"),Finite);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVamGluteDampingTest,"Vam.Glute.G1.AttachmentDamping",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FVamGluteDampingTest::RunTest(const FString& Parameters)
{
    auto* P=NewObject<UVamGluteJiggleProfile>();P->bNormalizedAttachmentDamping=true;
    auto R=MechanicsFixture();FVamGluteTuning T;
    for(double PelvisFraction:{.02,.25,.5,.75,.98})
    {
        for(auto& N:R.Nodes) { N.PelvisAttachment=PelvisFraction;N.ThighAttachment=1-PelvisFraction; }
        FVamGluteSolver S;
        S.Advance(*P,R,LinearMotion(0,0),0,FVector::ZeroVector,T);
        S.Advance(*P,R,LinearMotion(0,0),P->FixedStep,FVector::ZeroVector,T);
        for(int32 I=0;I<5;++I)
        {
            const double Zeta=S.Nodes[I].Damping.X/(2*FMath::Sqrt(R.Nodes[I].MassKg*R.Nodes[I].Support.X));
            TestTrue(TEXT("Parallel attachments preserve requested total damping ratio"),FMath::Abs(Zeta-R.Nodes[I].DampingRatio.X)<1.e-10);
        }
    }
    P->bNormalizedAttachmentDamping=false;T=FVamGluteTuning();FVamGluteSolver Legacy;
    Legacy.Advance(*P,R,LinearMotion(0,0),0,FVector::ZeroVector,T);Legacy.Advance(*P,R,LinearMotion(0,0),P->FixedStep,FVector::ZeroVector,T);
    const auto& N=R.Nodes[0];const double Expected=2*N.DampingRatio.X*FMath::Sqrt(N.MassKg*N.Support.X)*(FMath::Sqrt(N.PelvisAttachment)+FMath::Sqrt(N.ThighAttachment));
    TestTrue(TEXT("Serialized legacy assets retain legacy branch damping"),FMath::Abs(Legacy.Nodes[0].Damping.X-Expected)<1.e-10);
    return true;
}
#endif

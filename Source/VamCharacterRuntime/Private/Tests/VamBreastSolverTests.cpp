#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "VamBreastSolver.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVamBreastDynamicsTest,"Vam.Breast.Dynamics",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FVamBreastDynamicsTest::RunTest(const FString& Parameters)
{
    auto* P=NewObject<UVamBreastJiggleProfile>();
    FVamBreastSideProfile R;R.MassKg=.5;R.ImportedGravityLocal=FVector::ZeroVector;
    for(int32 I=0;I<5;++I) { FVamBreastNodeParameters N;N.Rest=FVector(10,0,0);N.PositiveLimitCm=N.NegativeLimitCm=FVector(10);R.Nodes.Add(N); }
    FVamBreastSolver S;const double H=1./120.;
    for(int32 I=0;I<1200;++I) S.Advance(*P,R,FTransform(FVector(I*H*100,0,0)),H,FVector::ZeroVector,false,false);
    TestTrue(TEXT("Constant velocity has no persistent displacement"),S.Nodes[0].Displacement.Size()<.001);
    S.Reset();S.LinearAcceleration=FVector(100,0,0);
    for(int32 I=0;I<60;++I) S.Step(*P,R,H,FVector::ZeroVector);
    TestTrue(TEXT("Positive acceleration produces negative lag"),S.Nodes[0].Displacement.X<-.01);
    S.LinearAcceleration=FVector(-100,0,0);
    for(int32 I=0;I<60;++I) S.Step(*P,R,H,FVector::ZeroVector);
    TestTrue(TEXT("Braking continues forward"),S.Nodes[0].Displacement.X>.01);
    S.LinearAcceleration=FVector::ZeroVector;
    for(int32 I=0;I<1200;++I) S.Step(*P,R,H,FVector::ZeroVector);
    TestTrue(TEXT("Braking recovers to rest"),S.Nodes[0].Displacement.Size()<.001);
    S.Reset();S.AngularVelocity=FVector(0,0,2);
    for(int32 I=0;I<1200;++I) S.Step(*P,R,H,FVector::ZeroVector);
    TestTrue(TEXT("Constant omega has radial equilibrium even with zero alpha"),S.Nodes[0].Displacement.X>.05);
    S.Reset();S.AngularAcceleration=FVector(0,0,2);
    for(int32 I=0;I<60;++I) S.Step(*P,R,H,FVector::ZeroVector);
    TestTrue(TEXT("Angular acceleration produces tangential lag"),S.Nodes[0].Displacement.Y<-.01);
    S.Reset();S.Nodes.SetNum(5);S.Nodes[0].Velocity=FVector(2,0,0);S.AngularVelocity=FVector(0,0,2);
    S.Step(*P,R,H,FVector::ZeroVector);
    TestTrue(TEXT("Coriolis bends relative velocity"),S.Nodes[0].Velocity.Y<0);
    const auto Frozen=S.Nodes;
    S.Advance(*P,R,FTransform(FVector(500,0,0)),H,FVector::ZeroVector,false,true);
    TestTrue(TEXT("Pause freezes displacement and velocity"),S.Nodes[0].Displacement.Equals(Frozen[0].Displacement,0) && S.Nodes[0].Velocity.Equals(Frozen[0].Velocity,0));
    S.Advance(*P,R,FTransform(FVector(10000,0,0)),H,FVector::ZeroVector,true,false);
    TestTrue(TEXT("Teleport clears invalid momentum"),S.Nodes[0].Velocity.IsNearlyZero());
    FVamBreastSolver Other;
    Other.Step(*P,R,H,FVector::ZeroVector);
    S.LinearAcceleration=FVector(400,0,0);S.Step(*P,R,H,FVector::ZeroVector);
    TestTrue(TEXT("Instances do not share state"),Other.Nodes[0].Displacement.IsNearlyZero() && !S.Nodes[0].Displacement.IsNearlyZero());
    TArray<FVector> Results;TArray<TArray<FVector>> Tracks;
    for(int32 FPS:{30,60,120})
    {
        FVamBreastSolver Instance;TArray<FVector> Track;
        for(int32 Frame=0;Frame<FPS*8;++Frame)
        {
            const double T=double(Frame)/FPS;
            const FTransform Anchor(FQuat(FVector::UpVector,.5*FMath::Sin(T*2)),FVector(20*FMath::Sin(T*3),0,0));
            Instance.Advance(*P,R,Anchor,1./FPS,FVector::ZeroVector,false,false);
            if(Frame%(FPS/30)==0 && !Instance.Nodes.IsEmpty()) Track.Add(Instance.Nodes[0].Displacement);
            for(const auto& N:Instance.Nodes) TestTrue(TEXT("Finite and bounded"),!N.Displacement.ContainsNaN() && !N.Velocity.ContainsNaN() && N.Displacement.Size()<20 && N.Velocity.Size()<1000);
        }
        Results.Add(Instance.Nodes[0].Displacement);Tracks.Add(Track);
    }
    TestTrue(TEXT("30/120 FPS bounded divergence < 0.3 cm"),(Results[0]-Results[2]).Size()<.3);
    TestTrue(TEXT("60/120 FPS bounded divergence < 0.2 cm"),(Results[1]-Results[2]).Size()<.2);
    double Maximum30=0,Maximum60=0;
    for(int32 I=0;I<Tracks[0].Num();++I)
    { Maximum30=FMath::Max(Maximum30,(Tracks[0][I]-Tracks[2][I]).Size());Maximum60=FMath::Max(Maximum60,(Tracks[1][I]-Tracks[2][I]).Size()); }
    TestTrue(TEXT("Full trajectory 30/120 divergence < .3 cm"),Maximum30<.3);
    TestTrue(TEXT("Full trajectory 60/120 divergence < .2 cm"),Maximum60<.2);
    AddInfo(FString::Printf(TEXT("Full trajectory max divergence: 30/120 %.6f cm; 60/120 %.6f cm"),Maximum30,Maximum60));
    FVamBreastSolver Light,Heavy;auto HeavyRest=R;auto LightRest=R;
    LightRest.ReferenceMassKg=LightRest.MassKg;HeavyRest.ReferenceMassKg=LightRest.MassKg;HeavyRest.MassKg*=2;
    Light.LinearAcceleration=Heavy.LinearAcceleration=FVector(30,0,0);
    for(int32 I=0;I<1200;++I) { Light.Step(*P,LightRest,H,FVector::ZeroVector);Heavy.Step(*P,HeavyRest,H,FVector::ZeroVector); }
    TestTrue(TEXT("Mass changes inertia while authored elasticity stays fixed"),FMath::Abs(Heavy.Nodes[0].Displacement.X)>FMath::Abs(Light.Nodes[0].Displacement.X)*1.5);
    // Long driven rotation followed by decay, detecting NaN and runaway energy.
    S.Reset();S.AngularVelocity=FVector(0,0,10);
    for(int32 I=0;I<12000;++I) S.Step(*P,R,H,FVector::ZeroVector);
    TestTrue(TEXT("Long rotation bounded"),!S.Nodes[0].Displacement.ContainsNaN() && S.Nodes[0].Velocity.Size()<1000);
    S.AngularVelocity=FVector::ZeroVector;
    for(int32 I=0;I<2400;++I) S.Step(*P,R,H,FVector::ZeroVector);
    TestTrue(TEXT("Unforced energy decays"),S.Nodes[0].Velocity.Size()<.01 && S.Nodes[0].Displacement.Size()<.01);
    return true;
}
#endif

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "VamGluteSolver.h"
#include "VamSecondaryGravity.h"
#include "HAL/PlatformTime.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVamReferenceGravityTest,"Vam.Glute.G11.ReferenceGravity",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FVamReferenceGravityTest::RunTest(const FString& Parameters)
{
    auto* P=NewObject<UVamGluteJiggleProfile>();P->SchemaVersion=2;P->bNormalizedAttachmentDamping=true;
    const FVector G(0,0,-980);const FQuat Imported=FRotator(13,27,-19).Quaternion();
    FVamGluteDynamicSide R;R.MassKg=1;R.ReferenceGravityLocal=Imported.UnrotateVector(G);
    for(int32 I=0;I<5;++I) { FVamGluteDynamicNode N;N.MassKg=.2;N.Rest=FVector(8,I-2,2-I);N.Support=FVector(100,120,140);N.PositiveTravel=N.NegativeTravel=FVector(4);N.PelvisAttachment=.8;N.ThighAttachment=.2;R.Nodes.Add(N); }
    const int32 Edges[][2]={{0,1},{0,2},{0,3},{0,4},{1,3},{1,4},{2,3},{2,4}};
    for(const auto& E:Edges) {FVamGluteDynamicEdge D;D.A=E[0];D.B=E[1];D.Stiffness=FVector(4);R.Couplings.Add(D);}
    FVamGluteTuning T;T.Support=.45;T.Damping=.65;T.Mobility=2;
    auto Motion=[&](const FQuat& Q){FVamGluteMotion M;M.Pelvis=M.Thigh=FTransform(Q);M.bKnownTwist=true;return M;};
    auto Settle=[&](FVamGluteSolver& S,const FVamGluteMotion& M,const FVector& Gravity){for(int32 I=0;I<2401;++I) S.Advance(*P,R,M,1./120,Gravity,T);};
    FVamGluteSolver S;Settle(S,Motion(Imported),G);
    TestTrue(TEXT("Nonidentity imported frame has zero baseline residual"),S.GravityResidualLocal.Size()<1.e-9 && S.Nodes[0].Displacement.Size()<1.e-8);
    const auto BeforeForceChange=S;
    S.Advance(*P,R,Motion(Imported),0,FVector::ZeroVector,T);
    TestTrue(TEXT("Changing force does not directly overwrite state"),S.Nodes[0].PositionWorld==BeforeForceChange.Nodes[0].PositionWorld && S.Nodes[0].VelocityWorld==BeforeForceChange.Nodes[0].VelocityWorld);
    for(const FRotator Rotation:{FRotator(90,0,0),FRotator(-90,0,0),FRotator(0,0,90),FRotator(0,0,-90),FRotator(0,90,0)})
    {
        const FQuat Q=Rotation.Quaternion()*Imported;S.Reset();Settle(S,Motion(Q),G);
        const FVector Expected=Q.UnrotateVector(G)-R.ReferenceGravityLocal;
        TestTrue(TEXT("Orientation residual and body preload agree"),S.GravityResidualLocal.Equals(Expected,1.e-9) && Q.RotateVector(Expected).Equals((S.GravityForce+S.GravityPreload)/R.MassKg,1.e-8));
        TestTrue(TEXT("Ordinary gravity equilibrium finite without hard clamp"),!S.Nodes[0].Displacement.ContainsNaN() && S.LimitCorrections==0);
        TestTrue(TEXT("Equilibrium follows residual load"),Expected.Size()<1.e-8?S.Nodes[0].Displacement.Size()<1.e-8:FVector::DotProduct(Expected,S.Nodes[0].Displacement)>0);
    }
    double Half=0,Zero=0;
    for(double Scale:{2.,1.,.5,0.,1.})
    {
        S.Reset();Settle(S,Motion(Imported),G*Scale);
        TestTrue(TEXT("Gravity magnitude has finite bounded equilibrium"),!S.Nodes[0].Displacement.ContainsNaN() && S.LimitCorrections==0);
        TestTrue(TEXT("Magnitude equilibrium direction"),Scale==1?S.Nodes[0].Displacement.Size()<1.e-8:FVector::DotProduct(S.Nodes[0].Displacement,R.ReferenceGravityLocal*(Scale-1))>0);
        if(Scale==.5) Half=S.Nodes[0].Displacement.Size();if(Scale==0) Zero=S.Nodes[0].Displacement.Size();
    }
    TestTrue(TEXT("Larger gravity difference has larger finite response"),Zero>Half && Half>0);
    TArray<FVector> ReferenceTrajectory;
    for(int32 FPS:{120,60,30})
    {
        FVamGluteSolver Transition;Transition.Advance(*P,R,Motion(Imported),0,G,T);double Peak=0,MaxJump=0;FVector Last=FVector::ZeroVector;
        for(int32 Frame=1;Frame<=FPS*16;++Frame)
        {
            const double Time=double(Frame)/FPS;const FVector Gravity=Time<=2?G:Time<=8?FVector::ZeroVector:G;
            Transition.Advance(*P,R,Motion(Imported),1./FPS,Gravity,T);
            const auto& N=Transition.Nodes[0];TestFalse(TEXT("Transition velocity finite"),N.VelocityWorld.ContainsNaN());
            Peak=FMath::Max(Peak,N.Displacement.Size());MaxJump=FMath::Max(MaxJump,(N.Displacement-Last).Size());Last=N.Displacement;
            if(Frame%(FPS/30)==0)
            {
                const int32 Index=Frame/(FPS/30)-1;
                if(FPS==120) ReferenceTrajectory.Add(N.Displacement);
                else TestTrue(TEXT("30/60/120 transition numerical agreement"),(N.Displacement-ReferenceTrajectory[Index]).Size()<.15);
            }
        }
        TestTrue(TEXT("Restored 1g damps to baseline without reset"),Transition.Nodes[0].Displacement.Size()<1.e-6 && Transition.LimitCorrections==0);
        TestTrue(TEXT("Transition is continuous with bounded overshoot"),MaxJump<Zero*.5 && Peak<Zero*2);
        AddInfo(FString::Printf(TEXT("Gravity transition FPS %d peak %.9f max frame change %.9f restored %.9f cm"),FPS,Peak,MaxJump,Transition.Nodes[0].Displacement.Size()));
    }
    double PreviousError=0;
    for(int32 FPS:{30,60,120,240,480})
    {
        P->FixedStep=1./FMath::Max(120,FPS);
        FVamGluteSolver A,B;double Error=0;
        for(int32 Frame=0;Frame<=FPS*2;++Frame)
        {
            const double Time=double(Frame)/FPS;auto MA=Motion(Imported),MB=MA;
            MB.Pelvis.SetLocation(G*(.5*Time*Time));MB.Thigh=MB.Pelvis;MB.PelvisVelocity=MB.ThighVelocity=G*Time;
            A.Advance(*P,R,MA,1./FPS,FVector::ZeroVector,T);B.Advance(*P,R,MB,1./FPS,G,T);
            for(int32 N=0;N<5;++N) Error=FMath::Max(Error,(A.Nodes[N].Displacement-B.Nodes[N].Displacement).Size());
        }
        AddInfo(FString::Printf(TEXT("Freefall / zero-g FPS %d maximum relative trajectory error %.9f cm"),FPS,Error));
        // Backward Euler follows prescribed analytical positions using endpoint
        // velocity. Uniform acceleration therefore has O(h) position error.
        // Verify convergence instead of disguising it as render-frame divergence.
        TestTrue(TEXT("Freefall relative error is bounded at 120 Hz"),Error<.3);
        if(FPS>120) TestTrue(TEXT("Halving physical step reduces freefall error"),Error<PreviousError*.6);
        PreviousError=Error;
    }
    P->FixedStep=1./120;
    double Costs[2]={};
    for(int32 Mode=0;Mode<2;++Mode) {P->SchemaVersion=Mode+1;FVamGluteSolver Bench;const double Start=FPlatformTime::Seconds();for(int32 I=0;I<12000;++I) Bench.Advance(*P,R,Motion(Imported),1./120,G,T);Costs[Mode]=(FPlatformTime::Seconds()-Start)*1.e6/12000;}
    AddInfo(FString::Printf(TEXT("Same-run solver legacy %.6f us reference-gravity %.6f us"),Costs[0],Costs[1]));
    P->SchemaVersion=1;S.Reset();Settle(S,Motion(FQuat::Identity),FVector::ZeroVector);
    TestTrue(TEXT("Old profiles retain explicitly legacy cancellation"),S.GravityResidualLocal.IsZero() && S.Nodes[0].Displacement.Size()<1.e-9);
    return true;
}
#endif

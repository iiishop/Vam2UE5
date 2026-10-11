#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "VamBreastWeightUtils.h"
#include "VamMorphNormalUtils.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVamBreastWeightTest,"Vam.Breast.WeightCompression",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FVamBreastWeightTest::RunTest(const FString& Parameters)
{
    TMap<int32,double> Weights;for(int32 I=0;I<8;++I) Weights.Add(I,I<3?.2:.08);
    const auto Before=Weights;TSet<int32> Donors={0,1,2};TArray<TPair<int32,double>> Helpers={{88,1.},{89,.5},{90,.2}};
    const int32 Compressed=VamBreastWeights::Redistribute(Weights,Donors,Helpers,.7);
    TestTrue(TEXT("Eight existing influences gain helper influence"),Compressed>0 && Weights.FindRef(88)>.1 && Weights.Num()<=8);
    double Sum=0;for(const auto& W:Weights) Sum+=W.Value;TestTrue(TEXT("Redistribution stays normalized"),FMath::Abs(Sum-1)<1.e-10);
    for(int32 I=3;I<8;++I) TestEqual(TEXT("Non-donor influence exactly preserved"),Weights.FindRef(I),Before.FindRef(I));
    TestTrue(TEXT("Approved source support retained"),Weights.FindRef(0)+Weights.FindRef(1)+Weights.FindRef(2)>.1);
    auto Root=Before;TestEqual(TEXT("Zero transfer leaves root intact"),VamBreastWeights::Redistribute(Root,Donors,Helpers,0),-1);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVamGluteFinalBudgetTest,"Vam.Glute.FinalInfluenceBudget",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FVamGluteFinalBudgetTest::RunTest(const FString& Parameters)
{
    const TSet<int32> Donors={0,1};const TArray<TPair<int32,double>> Helpers={{88,.3},{89,.25},{90,.2},{91,.15},{92,.1}};
    TMap<int32,double> Before={{0,.7},{1,.1},{2,.1999},{3,.0001}};auto After=Before;
    const double Discarded=VamBreastWeights::RedistributeFinalBudget(After,Donors,Helpers,.7);
    TestTrue(TEXT("Tiny non-donor must not evict a substantial static helper"),After.FindRef(92)>.05 && !After.Contains(3));
    TestTrue(TEXT("Bounded finite influence budget"),After.Num()==8 && FMath::Abs(Discarded-.0001)<1.e-10);
    double Sum=0;for(const auto& W:After)Sum+=W.Value;TestTrue(TEXT("Final weights normalized"),FMath::Abs(Sum-1)<1.e-12);
    TestTrue(TEXT("Both primary donors retained"),After.FindRef(0)>.2 && After.FindRef(1)>.02);
    auto WithoutTiny=Before;WithoutTiny.Remove(3);WithoutTiny.FindChecked(2)+=.0001;VamBreastWeights::RedistributeFinalBudget(WithoutTiny,Donors,Helpers,.7);
    for(const auto& H:Helpers)TestTrue(TEXT("Source slot-count change has only tiny effect on helper weights"),FMath::Abs(After.FindRef(H.Key)-WithoutTiny.FindRef(H.Key))<.0001);
    auto Zero=Before;VamBreastWeights::RedistributeFinalBudget(Zero,Donors,Helpers,0);TestEqual(TEXT("Zero transfer unchanged"),Zero.FindRef(3),Before.FindRef(3));
    TMap<int32,double> Dense={{0,.25},{1,.15},{2,.0999},{3,.0001},{88,.10},{89,.10},{90,.10},{91,.10},{92,.10}};
    const double Lost=VamBreastWeights::FinalizeBudget(Dense);
    TestTrue(TEXT("Fitted complete candidate retains all significant regions"),Dense.Num()==8 && Dense.FindRef(92)>.099 && FMath::Abs(Lost-.0001)<1.e-10);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVamMorphNormalOrientationTest,"Vam.Glute.MorphNormalOrientation",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FVamMorphNormalOrientationTest::RunTest(const FString& Parameters)
{
    const TArray<FVector> P={{0,0,0},{1,0,0},{0,1,0}};
    const TArray<FVector> D={{0,0,0},{0,0,0},{0,0,1}};
    for(double Sign:{-1.,1.})
    {
        const TArray<FVector> N={FVector(0,0,Sign),FVector(0,0,Sign),FVector(0,0,Sign)};
        const auto A=VamMorphNormals::Deltas(P,N,{0,1,2},D);
        const auto B=VamMorphNormals::Deltas(P,N,{0,2,1},D);
        const FVector Expected=FVector(0,-Sign,Sign).GetSafeNormal();
        for(int32 V=0;V<3;++V){TestTrue(TEXT("Morph normal follows the outward deformed surface"),(N[V]+A[V]).Equals(Expected,1.e-12));TestTrue(TEXT("Winding independent authored normal delta"),A[V].Equals(B[V],1.e-12));}
        const auto Zero=VamMorphNormals::Deltas(P,N,{0,1,2},{FVector::ZeroVector,FVector::ZeroVector,FVector::ZeroVector});
        for(const auto& V:Zero)TestTrue(TEXT("Zero displacement does not change authored shading"),V.IsNearlyZero());
    }
    return true;
}
#endif

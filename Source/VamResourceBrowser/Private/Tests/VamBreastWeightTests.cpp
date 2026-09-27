#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "VamBreastWeightUtils.h"
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
#endif

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "VamGluteCorrectiveProfile.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVamKneeDriverTest,"Vam.Knee.SourceOperatorDriver",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FVamKneeDriverTest::RunTest(const FString&)
{
    auto* P=NewObject<UVamGluteCorrectiveProfile>();FVamKneeCorrective K;K.Bone=1;K.Parent=0;K.ReferenceRotation=FQuat(FVector::XAxisVector,.04);
    K.Angles={0,30,60,90,120};for(int I=0;I<K.Angles.Num();++I)K.Morphs.Add(FName(*FString::Printf(TEXT("K%d"),I)));P->Knees.Add(K);
    for(double Angle:{0.,15.,30.,45.,90.,105.,120.})
    {
        TArray<FTransform> Pose={FTransform(FQuat(FVector::ZAxisVector,.7),FVector(11,12,13)),FTransform::Identity};
        Pose[1]=FTransform(K.ReferenceRotation*FQuat(FVector::YAxisVector,Angle*PI/180.),FVector(0,0,-30))*Pose[0];
        const auto W=VamGluteCorrective::KneeWeights(*P,Pose);double Total=0,Mean=0;
        for(int I=0;I<K.Angles.Num();++I){Total+=W.FindRef(K.Morphs[I]);Mean+=K.Angles[I]*W.FindRef(K.Morphs[I]);}
        TestTrue(TEXT("Partition of unity"),FMath::Abs(Total-1)<1.e-6);TestTrue(TEXT("Actual local twist reconstructed"),FMath::Abs(Mean-Angle)<1.e-4);
        auto Q=Pose[1].GetRotation();Pose[1].SetRotation(Q*-1.);const auto Flipped=VamGluteCorrective::KneeWeights(*P,Pose);
        for(FName N:K.Morphs)TestTrue(TEXT("Quaternion sign invariant"),FMath::Abs(W.FindRef(N)-Flipped.FindRef(N))<1.e-6);
    }
    return !HasAnyErrors();
}
#endif

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "VamRigProfile.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVamPoseEulerLimits,"Vam.Leg.EquivalentEulerLimits",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FVamPoseEulerLimits::RunTest(const FString&)
{
 FVamRigJoint J;J.bLimitRotation=true;J.Minimum=FRotator(-115,-50,-50);J.Maximum=FRotator(115,50,50);
 for(double Pitch:{-115.,-110.,-91.,-89.,0.,89.,91.,110.,115.})for(double Yaw:{-10.,0.,10.}){
  const FQuat Q=FRotator(Pitch,Yaw,3).Quaternion();const auto R=VamPoseControl::Clamp(J,Q.Rotator());
  TestTrue(FString::Printf(TEXT("Equivalent hinge %g / %g"),Pitch,Yaw),Q.Equals(R.Quaternion(),1.e-6));
 }
 for(double Pitch:{-160.,-120.,120.,160.}){
  const auto R=VamPoseControl::Clamp(J,FRotator(Pitch,0,0).Quaternion().Rotator());
  TestTrue(TEXT("Authored pitch limit unchanged"),FMath::Abs(R.Pitch)<=115&&FMath::Abs(R.Yaw)<=50&&FMath::Abs(R.Roll)<=50);
  TestTrue(TEXT("Hinge clamps to authored end"),R.Quaternion().Equals(FRotator(FMath::Sign(Pitch)*115,0,0).Quaternion(),1.e-6));
 }
 return true;
}
#endif

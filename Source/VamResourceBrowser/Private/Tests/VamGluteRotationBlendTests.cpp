#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "VamGluteRotationBlend.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVamGluteRotationBlendTest,"Vam.Glute.RotationBlendReference",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FVamGluteRotationBlendTest::RunTest(const FString& Parameters)
{
    using namespace VamGluteGeometry;FVamNativeMeshInput I;I.Vertices={FVector(4,2,1),FVector(-3,1,5)};
    TArray<FTransform> Bind={FTransform::Identity,FTransform::Identity},Pose=Bind;
    for(int32 V=0;V<2;++V) { FVamBuildInfluence W;W.Vertex=V;W.Bone=0;W.Weight=.5;I.Influences.Add(W);W.Bone=1;I.Influences.Add(W); }
    TestTrue(TEXT("Neutral identity"),RotationBlend(I,Bind,Pose)[0].Equals(I.Vertices[0],1.e-12));
    Pose[1]=FTransform(FQuat(FVector::ZAxisVector,PI/2));const auto Result=RotationBlend(I,Bind,Pose);
    // Quantized FBoneWeights are not exactly 0.5. Test the analytic normalized
    // quaternion blend using those same weights rather than assuming 45 deg.
    const auto Linear=Apply(Skin(I,I.Influences,Bind,Pose),I.Vertices,true);
    TestTrue(TEXT("Two rigid rotations retain point radius"),FMath::Abs(Result[0].Size()-I.Vertices[0].Size())<1.e-5);
    TestTrue(TEXT("LBS mixed rotation contracts"),Linear[0].Size()<I.Vertices[0].Size()-.5);
    const FTransform World(FQuat(FVector(1,2,3).GetSafeNormal(),.8),FVector(17,-21,9));auto Moved=Pose;for(auto& T:Moved) T=T*World;
    const auto Rigid=RotationBlend(I,Bind,Moved);for(int32 V=0;V<2;++V) TestTrue(TEXT("Rigid frame equivariance"),Rigid[V].Equals(World.TransformPosition(Result[V]),1.e-5));
    I.Influences.Reset();for(int32 V=0;V<2;++V) { FVamBuildInfluence W;W.Vertex=V;W.Bone=1;W.Weight=1;I.Influences.Add(W); }
    Pose[1].SetScale3D(FVector(1.2,.8,1.1));Pose[1].SetTranslation(FVector(2,-3,4));const auto Single=RotationBlend(I,Bind,Pose),Affine=Apply(Skin(I,I.Influences,Bind,Pose),I.Vertices,true);
    for(int32 V=0;V<2;++V) TestTrue(TEXT("Single influence preserves affine scaffold"),Single[V].Equals(Affine[V],1.e-10));
    Pose[1].SetRotation(Pose[1].GetRotation()*-1.);TestTrue(TEXT("Quaternion antipodes equivalent"),RotationBlend(I,Bind,Pose)[0].Equals(Single[0],1.e-10));
    return true;
}
#endif

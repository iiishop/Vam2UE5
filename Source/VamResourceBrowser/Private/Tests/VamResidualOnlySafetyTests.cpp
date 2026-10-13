#include "Misc/AutomationTest.h"
#include <limits>
#include "VamGluteCorrectiveGeometry.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVamResidualOnlySafetyTest,"Vam.Glute.ResidualOnlySafety",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FVamResidualOnlySafetyTest::RunTest(const FString&)
{
    using namespace VamGluteGeometry;
    FVamNativeMeshInput I;I.Vertices={FVector(0,0,0),FVector(1,0,0),FVector(0,1,0)};I.Triangles={0,1,2};
    TArray<FMatrix> M;M.Init(FMatrix::Identity,3);TMap<int32,TArray<int32>> Aliases;for(int V=0;V<3;++V)Aliases.Add(V,{V});
    FDeltaField Fixed;Fixed.Init(FVector::ZeroVector,3);Fixed[1]=FVector(-5,0,0);const auto Original=Fixed;
    FDeltaField Residual;Residual.Init(FVector::ZeroVector,3);TArray<double> A;
    TestTrue(TEXT("Zero residual preserves even an unattenuated fixed surface"),Condition(I,M,Residual,Aliases,A,&Fixed,true));
    for(double X:A)TestEqual(TEXT("No source attenuation is hidden in residual protection"),X,1.);
    Residual[1]=FVector(8,0,0);
    TestTrue(TEXT("Residual that would reverse fixed surface is constrained"),Condition(I,M,Residual,Aliases,A,&Fixed,true));
    TestTrue(TEXT("Protection still attenuates residual"),A[1]<1. && A[1]>0.);
    TestTrue(TEXT("Final orientation and area hold relative to fixed surface"),TriangleSafe(I.Vertices[0]+Fixed[0],I.Vertices[1]+Fixed[1],I.Vertices[2]+Fixed[2],Residual[0]*A[0],Residual[1]*A[1],Residual[2]*A[2],1,.05));
    TestTrue(TEXT("Residual native edge budget holds"),(Residual[1]*A[1]-Residual[0]*A[0]).Size()<3.);
    for(int V=0;V<3;++V)TestEqual(TEXT("Fixed input never changed"),Fixed[V],Original[V]);
    auto Mirror=I;auto MirrorFixed=Fixed;auto MirrorResidual=Residual;for(auto& V:Mirror.Vertices)V.Y=-V.Y;for(auto& V:MirrorFixed)V.Y=-V.Y;for(auto& V:MirrorResidual)V.Y=-V.Y;TArray<double> B;
    TestTrue(TEXT("Mirrored reference has identical protection"),Condition(Mirror,M,MirrorResidual,Aliases,B,&MirrorFixed,true));
    for(int V=0;V<3;++V)TestTrue(TEXT("Protection does not depend on side"),FMath::Abs(A[V]-B[V])<1.e-9);
    Residual.Init(FVector::ZeroVector,3);Fixed[0].X=std::numeric_limits<double>::quiet_NaN();
    TestFalse(TEXT("Non-finite source is still rejected"),Condition(I,M,Residual,Aliases,B,&Fixed,true));
    return true;
}

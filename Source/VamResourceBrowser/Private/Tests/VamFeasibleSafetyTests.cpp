#include "Misc/AutomationTest.h"
#include "VamGluteCorrectiveGeometry.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVamFeasibleGluteSafetyTest,"Vam.Glute.FeasibleSafetyGrowth",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FVamFeasibleGluteSafetyTest::RunTest(const FString&)
{
 using namespace VamGluteGeometry;
 FVamNativeMeshInput I;I.Vertices={FVector(0,0,0),FVector(1,0,0),FVector(0,1,0),FVector(1,1,0)};I.Triangles={0,1,2,2,1,3};
 TArray<FMatrix> M;M.Init(FMatrix::Identity,4);TMap<int32,TArray<int32>> Aliases;for(int V=0;V<4;++V)Aliases.Add(V,{V});
 FDeltaField D;D.Init(FVector(2,0,1),4);TArray<double> A;
 TestTrue(TEXT("Rigid translation is retained"),GrowSafeField(I,M,D,Aliases,A,nullptr));for(double X:A)TestEqual(TEXT("Translation keeps full amplitude"),X,1.);
 D.Init(FVector::ZeroVector,4);D[1]=FVector(-3,0,0);D[3]=FVector(-1,.3,.2);
 TestTrue(TEXT("Unsafe input receives feasible continuation"),GrowSafeField(I,M,D,Aliases,A,nullptr));
 TestTrue(TEXT("Nonzero correction retained"),A[1]>.01);TestTrue(TEXT("Unsafe field actually constrained"),A[1]<1);
 for(int T=0;T<I.Triangles.Num();T+=3){int U=I.Triangles[T],V=I.Triangles[T+1],W=I.Triangles[T+2];TestTrue(TEXT("Independent final triangle predicate"),TriangleSafe(I.Vertices[U],I.Vertices[V],I.Vertices[W],D[U]*A[U],D[V]*A[V],D[W]*A[W],1,.05));}
 auto Mirrored=I;auto MirrorD=D;for(auto& V:Mirrored.Vertices)V.Y=-V.Y;for(auto& V:MirrorD)V.Y=-V.Y;TArray<double> B;
 TestTrue(TEXT("Mirrored input converges"),GrowSafeField(Mirrored,M,MirrorD,Aliases,B,nullptr));for(int V=0;V<4;++V)TestTrue(TEXT("No side-specific update rule"),FMath::Abs(A[V]-B[V])<1.e-9);
 TArray<FDeltaField> Bases{D};Bases[0][1]=FVector(0,0,3);Bases[0][3]=FVector(.2,0,-2);
 TestTrue(TEXT("All basis fields share a feasible attenuation"),GrowSafeField(I,M,D,Aliases,B,nullptr,&Bases));
 Bases.Add(D);for(const auto& Field:Bases)for(int T=0;T<I.Triangles.Num();T+=3){int U=I.Triangles[T],V=I.Triangles[T+1],W=I.Triangles[T+2];TestTrue(TEXT("Every jointly solved basis remains safe"),TriangleSafe(I.Vertices[U],I.Vertices[V],I.Vertices[W],Field[U]*B[U],Field[V]*B[V],Field[W]*B[W],1,.05));}
 auto Split=I;Split.Vertices.Add(I.Vertices[1]);Split.Triangles[4]=4;auto SplitM=M;SplitM.Add(M[1]);auto SplitD=D;SplitD.Add(D[1]);auto SplitAliases=Aliases;SplitAliases[1].Add(4);
 TestTrue(TEXT("Source-proven split vertices remain feasible"),GrowSafeField(Split,SplitM,SplitD,SplitAliases,B,nullptr));TestEqual(TEXT("Alias group receives identical attenuation"),B[1],B[4]);
 AddExpectedError(TEXT("GLUTE_GROWTH_FIXED_UNSAFE"),EAutomationExpectedErrorFlags::Contains,1);
 FDeltaField Zero;Zero.Init(FVector::ZeroVector,4);TestFalse(TEXT("Unsafe fixed field is rejected, never bypassed"),GrowSafeField(I,M,Zero,Aliases,B,&D));
 return true;
}

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

#include "Misc/AutomationTest.h"
#include "VamGluteFamilySafety.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVamJointFamilySafetyTest,"Vam.Glute.JointFamilySafety",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FVamJointFamilySafetyTest::RunTest(const FString&)
{
 using namespace VamGluteGeometry;
 FVamNativeMeshInput I;I.Vertices={FVector(0,0,0),FVector(1,0,0),FVector(0,1,0),FVector(1,1,0)};I.Triangles={0,1,2,2,1,3};
 TMap<int32,TArray<int32>> Aliases;for(int V=0;V<4;++V)Aliases.Add(V,{V});
 FDeltaField Zero;Zero.Init(FVector::ZeroVector,4);
 auto MakePose=[&](const FVamNativeMeshInput& Mesh){auto P=MakeShared<FGluteJointSafety::FPose>();P->Skin.Init(FMatrix::Identity,Mesh.Vertices.Num());P->Base=Mesh.Vertices;for(int T=0;T<Mesh.Triangles.Num();T+=3)P->Normals.Add(FVector::CrossProduct(P->Base[Mesh.Triangles[T+1]]-P->Base[Mesh.Triangles[T]],P->Base[Mesh.Triangles[T+2]]-P->Base[Mesh.Triangles[T]]).GetSafeNormal());return P;};
 TArray<FDeltaField> Goals;Goals.SetNum(2);Goals[0].Init(FVector(2,0,1),4);Goals[1]=Zero;Goals[1][1]=FVector(-3,0,0);Goals[1][3]=FVector(-1,.3,.2);
 FGluteJointSafety S(I,Goals,Aliases);auto P=MakePose(I);S.AddScene(P,{1,0},Zero,false,true);S.AddScene(P,{0,1},Zero,false,true);S.AddScene(P,{.5,.5},Zero,false,true);
 TestTrue(TEXT("All poses jointly solve"),S.Solve());TestTrue(TEXT("All original predicates hold after exact reconstruction"),S.AllValid(false));
 for(double A:S.Alpha[0])TestEqual(TEXT("Safe rigid field is not erased by another target"),A,1.);
 TestTrue(TEXT("Unsafe local target remains constrained"),S.Alpha[1][1]<1.);
 auto Mirror=I;auto MirrorGoals=Goals;for(auto& V:Mirror.Vertices)V.Y=-V.Y;for(auto& F:MirrorGoals)for(auto& V:F)V.Y=-V.Y;
 FGluteJointSafety M(Mirror,MirrorGoals,Aliases);auto MP=MakePose(Mirror);M.AddScene(MP,{1,0},Zero,false,true);M.AddScene(MP,{0,1},Zero,false,true);M.AddScene(MP,{.5,.5},Zero,false,true);TestTrue(TEXT("Mirrored geometry solves"),M.Solve());
 for(int K=0;K<Goals.Num();++K)for(int V=0;V<4;++V)TestTrue(TEXT("Mirror keeps equivalent safety"),FMath::Abs(M.Alpha[K][V]-S.Alpha[K][V])<1.e-8);
 FGluteJointSafety Bad(I,Goals,Aliases);Bad.AddScene(P,{0,0},Goals[1],true,true);AddExpectedError(TEXT("GLUTE_JOINT_FIXED_UNSAFE"),EAutomationExpectedErrorFlags::Contains,1);TestFalse(TEXT("Unsafe fixed input rejected"),Bad.Solve());
 // Every one-ring contains a locked centre, but the outer vertices are safe
 // independent degrees of freedom. Block-only recovery must not freeze them.
 FVamNativeMeshInput Fan;Fan.Vertices={FVector(0,0,0),FVector(1,0,0),FVector(0,1,0),FVector(-1,0,0),FVector(0,-1,0)};Fan.Triangles={0,1,2,0,2,3,0,3,4,0,4,1};
 TMap<int32,TArray<int32>> FanAliases;for(int V=0;V<5;++V)FanAliases.Add(V,{V});
 TArray<FDeltaField> FanGoal;FanGoal.SetNum(1);FanGoal[0].Init(FVector(0,0,.01),5);FanGoal[0][0]=FVector(-5,0,0);FDeltaField FanZero;FanZero.Init(FVector::ZeroVector,5);
 auto FP=MakePose(Fan);FP->Skin[0].M[0][0]=.001;
 FGluteJointSafety FS(Fan,FanGoal,FanAliases);FS.AddScene(FP,{1},FanZero,false,true);TestTrue(TEXT("Locked centre remains protected"),FS.Solve());
 for(int V=1;V<5;++V)TestTrue(TEXT("Safe outer vertex is not frozen by its locked neighbour"),FS.Alpha[0][V]>.99999);
 TestTrue(TEXT("Full original checks still pass for fan"),FS.AllValid(false));
 // The final composed surface can be safe even when a scalar attenuation
 // gradient exceeds the former surrogate budget. Test the surface itself.
 auto SurfaceGoals=FanGoal;SurfaceGoals[0].Init(FVector(0,0,1),5);SurfaceGoals[0][0]=FVector(0,0,5);
 FGluteJointSafety SurfaceOnly(Fan,SurfaceGoals,FanAliases);SurfaceOnly.AddScene(FP,{1},FanZero,false,true);
 TestTrue(TEXT("Surface-constrained recovery succeeds"),SurfaceOnly.Solve());
 for(int V=1;V<5;++V)TestTrue(TEXT("Safe actual surface is not reduced by a surrogate mask gradient"),SurfaceOnly.Alpha[0][V]>.99999);
 TestTrue(TEXT("All actual surface predicates still hold"),SurfaceOnly.AllValid(false));

 return true;
}

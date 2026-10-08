#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "VamBreastContactProfile.h"
#include "FTetWildWrapper.h"
#include "Generate/IsosurfaceStuffing.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAABBTree3.h"
#include "Spatial/FastWinding.h"

// Offline experiment only. Never changes a committed character or runtime defaults.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVamContactMeshingTest,"Vam.Breast.ContactMeshingComparison",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FVamContactMeshingTest::RunTest(const FString&)
{
 FString Path=TEXT("/Game/VamRuntime/R_05393c71101a04b1d7f0ed32/DA_BreastContact"),Out;
 FParse::Value(FCommandLine::Get(),TEXT("VamMeshingProfile="),Path);
 if(!FParse::Value(FCommandLine::Get(),TEXT("VamMeshingOutput="),Out)){AddError(TEXT("Explicit output directory required"));return false;}
 auto* P=LoadObject<UVamBreastContactProfile>(nullptr,*Path);
 if(!TestNotNull(TEXT("Source profile"),P))return false;
 IFileManager::Get().MakeDirectory(*Out,true);
 for(int32 Side=0;Side<2;++Side)
 {
  TArray<FVector> Original,Surface;TArray<FIntVector4> OriginalTets;TArray<FIntVector> Faces;
  TMap<int32,int32> AllMap,SurfaceMap;
  for(int32 I=0;I<P->Particles.Num();++I)if(P->Particles[I].Side==Side){AllMap.Add(I,Original.Num());Original.Add(P->Particles[I].Rest);}
  for(auto T:P->Tetrahedra)if(P->Particles[T[0]].Side==Side){for(int32 J=0;J<4;++J)T[J]=AllMap[T[J]];OriginalTets.Add(T);}
  for(auto F:P->BoundaryTriangles)if(P->Particles[F[0]].Side==Side)
  {for(int32 J=0;J<3;++J){const int32 Old=F[J];if(!SurfaceMap.Contains(Old)){SurfaceMap.Add(Old,Surface.Num());Surface.Add(P->Particles[Old].Rest);}F[J]=SurfaceMap[Old];}Faces.Add(F);}
  auto Export=[&](const FString& Name,const TArray<FVector>& V,const TArray<FIntVector4>& T,double Seconds)
  {
   FString S=FString::Printf(TEXT("{\"method\":\"%s\",\"side\":%d,\"generation_seconds\":%.9g,\"vertices\":["),*Name,Side,Seconds);
   for(int32 I=0;I<V.Num();++I)S+=FString::Printf(TEXT("%s[%.12g,%.12g,%.12g]"),I?TEXT(","):TEXT(""),V[I].X,V[I].Y,V[I].Z);
   S+=TEXT("],\"tets\":[");for(int32 I=0;I<T.Num();++I)S+=FString::Printf(TEXT("%s[%d,%d,%d,%d]"),I?TEXT(","):TEXT(""),T[I][0],T[I][1],T[I][2],T[I][3]);S+=TEXT("]}");
   TestTrue(TEXT("Export mesh"),FFileHelper::SaveStringToFile(S,*(Out/FString::Printf(TEXT("%s-%d.json"),*Name,Side))));
   AddInfo(FString::Printf(TEXT("%s side %d: %d particles %d tets %.3fs"),*Name,Side,V.Num(),T.Num(),Seconds));
  };
  Export(TEXT("layered"),Original,OriginalTets,0);
  // Fast winding requires an outward surface; runtime cage face orientation is
  // not its volume convention. Normalize input for BOTH native generators.
  double SurfaceVolume=0;for(auto F:Faces)SurfaceVolume+=FVector::DotProduct(Surface[F.X],FVector::CrossProduct(Surface[F.Y],Surface[F.Z]))/6.;
  if(SurfaceVolume<0)for(auto& F:Faces)Swap(F.Y,F.Z);
  UE::Geometry::FDynamicMesh3 Mesh;for(auto V:Surface)Mesh.AppendVertex(V);for(auto F:Faces)TestTrue(TEXT("Manifold face inserted"),Mesh.AppendTriangle(F.X,F.Y,F.Z)>=0);
  UE::Geometry::FDynamicMeshAABBTree3 Spatial(&Mesh);UE::Geometry::TFastWindingTree<UE::Geometry::FDynamicMesh3> Winding(&Spatial);
  for(int32 Cells:{6,9})
  {
   UE::Geometry::TIsosurfaceStuffing<double> Iso;auto Bounds=Spatial.GetBoundingBox();Iso.Bounds=FBox(Bounds);Iso.CellSize=Bounds.MaxDim()/Cells;Iso.IsoValue=0;
   // Signed distance zero contour keeps the input surface, without a half-cm erosion.
   Iso.Implicit=[&](FVector3d Pos){return FVector3d::Distance(Spatial.FindNearestPoint(Pos),Pos)*FMathd::SignNonZero(FMath::Abs(Winding.FastWindingNumber(Pos))-.5);};
   AddInfo(FString::Printf(TEXT("Iso center winding %.6f, faces %d/%d"),Winding.FastWindingNumber(Bounds.Center()),Mesh.TriangleCount(),Faces.Num()));
   const double Start=FPlatformTime::Seconds();Iso.Generate();
   TArray<FVector> V;for(auto X:Iso.Vertices)V.Add(X);TArray<FIntVector4> T;for(auto X:Iso.Tets)T.Add(X);
   TestTrue(TEXT("Iso generated cells"),T.Num()>0);Export(FString::Printf(TEXT("iso%d"),Cells),V,T,FPlatformTime::Seconds()-Start);
  }
  for(double Edge:{.15,.10})
  {
   UE::Geometry::FTetWild::FTetMeshParameters Params;Params.IdealEdgeLengthRel=Edge;Params.MaxIts=40;Params.EpsRel=.001;
   TArray<FVector> V;TArray<FIntVector4> T;const double Start=FPlatformTime::Seconds();
   TestTrue(TEXT("UE TetWild generation"),UE::Geometry::FTetWild::ComputeTetMesh(Params,Surface,Faces,V,T));
   Export(FString::Printf(TEXT("tetwild%d"),FMath::RoundToInt(Edge*100)),V,T,FPlatformTime::Seconds()-Start);
  }
 }
 return true;
}
#endif

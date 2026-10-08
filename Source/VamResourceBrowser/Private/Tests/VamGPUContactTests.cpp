#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "VamGPUContact.h"
#include "VamRuntimeConfiguration.h"
#include "VamBreastContactProfile.h"
#include "VamBreastJiggleProfile.h"
#include "Engine/SkeletalMesh.h"
#include "RenderingThread.h"
#include "VamBreastContactBuilder.h"
#include "UObject/SavePackage.h"
#include "Misc/PackageName.h"
#include "OptimusDeformer.h"
#include "ShaderCompiler.h"
#include "ComputeFramework/ComputeFramework.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVamGPUContactTest,"Vam.Breast.GPUContact",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FVamGPUContactTest::RunTest(const FString&)
{
 FString Path;FParse::Value(FCommandLine::Get(),TEXT("VamBreastTestConfig="),Path);
 auto* C=LoadObject<UVamRuntimeConfiguration>(nullptr,*Path);auto* P=C?C->BreastContact.LoadSynchronous():nullptr;
 if(!TestNotNull(TEXT("Contact profile"),P))return false;
 auto* J=C->BreastJiggle.LoadSynchronous();auto* Mesh=P->Body.LoadSynchronous();if(!J||!Mesh)return false;
 const auto& Ref=Mesh->GetRefSkeleton();TArray<FTransform> Pose=Ref.GetRefBonePose();
 for(int I=0;I<Pose.Num();++I)if(Ref.GetParentIndex(I)>=0)Pose[I]*=Pose[Ref.GetParentIndex(I)];
 const auto& Side=J->Sides[0];const FTransform Frame=Pose[Side.AnchorBone];const float Radius=Side.EffectiveRadiusCm*.55;
 TArray<FVector> Rest;for(const auto& Q:P->Particles)Rest.Add(Q.Rest);
 FVector Local=Side.COM;Local.X=P->ProbeFront(Rest,Frame,Local,Radius,false,true,0)+Radius-Side.EffectiveDepthCm*.2;
 const FVector Center=Frame.TransformPosition(Local);
 FVamGPUContactInput Input;const int N=Rest.Num();TArray<double> Mass;Mass.Init(0,N);
 for(auto T:P->Tetrahedra){const double M=P->SignedTetVolume(Rest,T)*P->DensityKgPerCm3/4.;for(int K=0;K<4;++K)Mass[T[K]]+=M;}
 for(int Batch=0;Batch<2;++Batch)
 {
  const FVector Shift(0,Batch*150,0);Input.Spheres.Add(FVector4f(FVector3f(Center+Shift),Radius));
  for(int I=0;I<N;++I){Input.Rest.Add(FVector4f(FVector3f(Rest[I]+Shift),P->Particles[I].bKinematic?0:float(1/FMath::Max(Mass[I],P->MinimumMovableMassKg))));Input.Instance.Add(Batch);}
  for(auto T:P->Tetrahedra)Input.Tets.Add(FIntVector4(T[0]+Batch*N,T[1]+Batch*N,T[2]+Batch*N,T[3]+Batch*N));
 }
 FString OutDir=FPaths::ProjectSavedDir()/TEXT("ContactGPU");FParse::Value(FCommandLine::Get(),TEXT("VamGPUOutput="),OutDir);IFileManager::Get().MakeDirectory(*OutDir,true);
 FString CSV=TEXT("jacobi,iterations,repeat,wall_ms,colors,buffer_bytes,max_volume_error,min_tet_ratio,inversions,root_error_cm,max_displacement_cm,max_particle_penetration_cm\n");
 for(bool Jacobi:{false,true})for(int Iter:{12,24,48,96})for(int Repeat=0;Repeat<3;++Repeat)
 {
  Input.bJacobi=Jacobi;Input.Iterations=Iter;FVamGPUContactResult Result;
  if(!TestTrue(TEXT("GPU readback succeeds"),VamRunGPUContactExperiment(Input,Result))){AddError(Result.Error);return false;}
  double Error=0,MinJ=1,RootError=0,Displacement=0,Penetration=0;int Inversions=0;
  for(int Batch=0;Batch<2;++Batch)
  {
   TArray<FVector> Current;
   for(int I=0;I<N;++I){const FVector Q=FVector(FVector3f(Result.Positions[Batch*N+I]))-FVector(0,Batch*150,0);Current.Add(Q);TestFalse(TEXT("Finite GPU output"),Q.ContainsNaN());
    const double D=(Q-Rest[I]).Size();Displacement=FMath::Max(Displacement,D);if(P->Particles[I].bKinematic)RootError=FMath::Max(RootError,D);
    else Penetration=FMath::Max(Penetration,Radius-(Q-Center).Size());}
   TArray<FVamBreastContactVolumeState> State;P->MeasureVolume(Rest,Current,State);
   for(const auto& V:State){Error=FMath::Max(Error,FMath::Abs(V.RelativeVolumeError));MinJ=FMath::Min(MinJ,V.MinimumTetRatio);Inversions+=V.InvertedTetrahedra;}
  }
  TestTrue(TEXT("Kinematic root preserved"),RootError<.001);
  if(Jacobi){TestEqual(TEXT("Jacobi no inverted tetrahedra"),Inversions,0);TestTrue(TEXT("Jacobi volume error below 1 percent for this fixture"),Error<.01);TestTrue(TEXT("Jacobi particle penetration below 0.001cm"),Penetration<.001);}

  CSV+=FString::Printf(TEXT("%d,%d,%d,%.6f,%d,%llu,%.8f,%.8f,%d,%.8f,%.8f,%.8f\n"),int(Jacobi),Iter,Repeat,Result.SynchronizedWallMs,Result.Colors,Result.BufferBytes,Error,MinJ,Inversions,RootError,Displacement,Penetration);
  if(Repeat==2){TArray<uint8> Bytes;Bytes.Append(reinterpret_cast<const uint8*>(Result.Positions.GetData()),Result.Positions.Num()*sizeof(FVector4f));FFileHelper::SaveArrayToFile(Bytes,*(OutDir/FString::Printf(TEXT("positions-%d-%d.bin"),int(Jacobi),Iter)));}
  AddInfo(FString::Printf(TEXT("GPU %d iterations %.3f ms, volume %.5f minJ %.5f inverted %d"),Iter,Result.SynchronizedWallMs,Error,MinJ,Inversions));
 }
 // Neutral and isolated-instance checks for the retained Jacobi candidate.
 Input.bJacobi=true;Input.Iterations=48;
 Input.Spheres[1]=FVector4f(10000,10000,10000,1);
 FVamGPUContactResult Isolated;
 if(TestTrue(TEXT("Isolated GPU batch"),VamRunGPUContactExperiment(Input,Isolated)))
 {
  double IdleDrift=0;
  for(int I=0;I<N;++I)IdleDrift=FMath::Max(IdleDrift,double((FVector3f(Isolated.Positions[N+I])-FVector3f(Input.Rest[N+I])).Size()));
  TestTrue(TEXT("Unpressed second instance remains neutral"),IdleDrift<.001);
  Input.Spheres[0]=Input.Spheres[1];FVamGPUContactResult Neutral;
  if(TestTrue(TEXT("Neutral GPU batch"),VamRunGPUContactExperiment(Input,Neutral)))
  {
   double Drift=0;for(int I=0;I<Input.Rest.Num();++I)Drift=FMath::Max(Drift,double((FVector3f(Neutral.Positions[I])-FVector3f(Input.Rest[I])).Size()));
   TestTrue(TEXT("Neutral shape preserved"),Drift<.001);
  }
 }
 FFileHelper::SaveStringToFile(CSV,*(OutDir/TEXT("timings.csv")));
 AddInfo(TEXT("GPU kernel validation only. Not full runtime FPS; material model differs from native GS and is not enabled on characters."));return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVamGPUContactAssetsTest,"Vam.Breast.GPUAssets",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FVamGPUContactAssetsTest::RunTest(const FString&)
{
 FString Path;FParse::Value(FCommandLine::Get(),TEXT("VamBreastTestConfig="),Path);
 auto* Source=LoadObject<UVamRuntimeConfiguration>(nullptr,*Path);if(!Source)return false;
 FString Root;FParse::Value(FCommandLine::Get(),TEXT("VamGPUAssetRoot="),Root);if(Root.IsEmpty())return false;
 const FString RCPath=Root/TEXT("RC_Runtime");if(FPackageName::DoesPackageExist(RCPath)){AddError(TEXT("Choose a fresh GPU asset root"));return false;}
 auto* RC=DuplicateObject<UVamRuntimeConfiguration>(Source,CreatePackage(*RCPath),TEXT("RC_Runtime"));
 auto* P=DuplicateObject<UVamBreastContactProfile>(Source->BreastContact.LoadSynchronous(),CreatePackage(*(Root/TEXT("DA_BreastContact"))),TEXT("DA_BreastContact"));
 const FString Error=UVamBreastContactBuilder::BuildGPUDeformer(P,Root/TEXT("DG_BreastContact_GPU"));if(!Error.IsEmpty()){AddError(Error);return false;}
 if(GShaderCompilingManager)GShaderCompilingManager->FinishAllCompilation();ComputeFramework::TickCompilation(0);
 RC->BreastContact=P;
 for(UObject* Asset:{static_cast<UObject*>(P->GPUSurfaceDeformer),static_cast<UObject*>(P),static_cast<UObject*>(RC)})
 {
  Asset->SetFlags(RF_Public|RF_Standalone);auto* Package=Asset->GetOutermost();FSavePackageArgs Args;Args.TopLevelFlags=RF_Public|RF_Standalone;
  TestTrue(TEXT("Save GPU candidate asset"),UPackage::SavePackage(Package,Asset,*FPackageName::LongPackageNameToFilename(Package->GetName(),FPackageName::GetAssetPackageExtension()),Args));
 }
 AddInfo(TEXT("New GPU RC ")+RCPath);return true;
}
#endif

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
#include "Engine/StaticMesh.h"
#include "RenderingThread.h"
#include "RHICommandList.h"
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVamGPUCouplingTest,"Vam.Breast.GPUCoupling",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FVamGPUCouplingTest::RunTest(const FString&)
{
 auto Build=[](){FVamGPUContactTopology T;T.Tets={FIntVector4(0,4,1,3),FIntVector4(1,4,2,3),FIntVector4(2,4,0,3)};T.Regions.Init(0,5);T.Material.Init(FVector4f(10,40,1,.2f),5);
  T.SkinFaces={FIntVector4(0,1,4,4),FIntVector4(1,2,4,4),FIntVector4(2,0,4,4),FIntVector4(0,3,1,1),FIntVector4(1,3,2,2),FIntVector4(2,3,0,0)};
  T.SkinHinges={FIntVector4(0,1,2,3),FIntVector4(1,2,0,3),FIntVector4(2,0,1,3)};
  for(int A=0;A<5;++A)for(int B=A+1;B<5;++B){T.SurfaceEdges.Add(FIntPoint(A,B));T.EdgeLimits.Add(FVector2f(.5f,2.f));}
  T.Parents.Add(FIntVector4(0,1,2,3));T.Weights.Add(FVector4f(.25f,.25f,.25f,.25f));T.Mask.Add(1);return T;};
 auto* A=NewObject<UStaticMesh>();auto* B=NewObject<UStaticMesh>();A->AddToRoot();B->AddToRoot();
 auto H0=VamGPURegister(A,Build()),H1=VamGPURegister(B,Build());
 const TArray<FVector4f> Points={FVector4f(0,-1,-1,100),FVector4f(0,1,-1,100),FVector4f(0,0,1,100),FVector4f(-2,0,0,0),FVector4f(0,0,-1.f/3,100)};
 auto Step=[&](double Separation,bool Soft,uint32 WorldB){
  for(int I=0;I<2;++I){FVamGPUContactScene Scene;Scene.WorldId=I?WorldB:1;Scene.bSoftCollision=Soft;Scene.BodyToWorld=I?FTransform(FRotator(0,180,0),FVector(Separation,0,0)):FTransform::Identity;
   auto H=I?H1:H0;VamGPUSetScene(H,MoveTemp(Scene));TArray<FVector4f> Rest=Points,Spheres,Starts;VamGPUUpdate(H,MoveTemp(Rest),MoveTemp(Spheres),MoveTemp(Starts));}
  VamGPUFlushBatch();ENQUEUE_RENDER_COMMAND(VamCouplingTestFence)([](FRHICommandListImmediate& Cmd){Cmd.SubmitAndBlockUntilGPUIdle();});FlushRenderingCommands();};
 for(int I=0;I<61;++I)Step(.5,true,1);
 TArray<FVector> R0,X0,R1,X1;TestTrue(TEXT("Resident neutral diagnostics"),VamGPUReadDiagnostic(H0,R0,X0)>0);
 double Neutral=0;for(int I=0;I<X0.Num();++I)Neutral=FMath::Max(Neutral,(X0[I]-R0[I]).Size());TestTrue(TEXT("Separated soft bodies remain neutral"),Neutral<.01);
 for(int I=0;I<90;++I)Step(.5-.7*FMath::Min(1.,I/30.),true,1);
 VamGPUReadDiagnostic(H0,R0,X0);VamGPUReadDiagnostic(H1,R1,X1);
 if(TestEqual(TEXT("Both resident cages readable"),X0.Num(),5)&&TestEqual(TEXT("Second cage readable"),X1.Num(),5)){
  TestTrue(TEXT("Both bodies deform away from contact"),X0[4].X<-.02 && X1[4].X<-.02);
  TestTrue(TEXT("Mirrored response"),FMath::Abs(X0[4].X-X1[4].X)<.03);
  TestTrue(TEXT("Anchors unchanged"),X0[3].Equals(R0[3],1e-5)&&X1[3].Equals(R1[3],1e-5));
  for(auto X:X0)TestFalse(TEXT("Finite soft output"),X.ContainsNaN());}
 TArray<FVamGPUContactLoad> L0,L1;double Time;TestTrue(TEXT("Async reaction packet"),VamGPUReadLoads(H0,L0,Time)>0);VamGPUReadLoads(H1,L1,Time);
 FVector F0=FVector::ZeroVector,F1=FVector::ZeroVector;for(auto L:L0)F0+=L.ForceNewtons;for(auto L:L1)F1+=L.ForceNewtons;
 TestTrue(TEXT("Equal and opposite soft reactions"),(F0+F1).Size()<1e-4);TestTrue(TEXT("Nonzero soft reaction"),F0.Size()>.0001);
 AddInfo(FString::Printf(TEXT("COUPLING neutral=%g displacement=%s / %s force=%s / %s"),Neutral,X0.Num()?*X0[4].ToString():TEXT("absent"),X1.Num()?*X1[4].ToString():TEXT("absent"),*F0.ToString(),*F1.ToString()));
 for(int I=0;I<90;++I)Step(-.2,true,2);
 VamGPUReadDiagnostic(H0,R0,X0);double Isolated=0;for(int I=0;I<X0.Num();++I)Isolated=FMath::Max(Isolated,(X0[I]-R0[I]).Size());TestTrue(TEXT("Different worlds do not collide"),Isolated<.01);

 for(int Kind=0;Kind<4;++Kind){
  for(int I=0;I<100;++I){FVamGPUContactScene Scene;Scene.WorldId=1;Scene.ColliderIds.Add(123);Scene.ShapeRotations.Add(FVector4f(0,0,0,1));Scene.ShapeExtents.Add(Kind==1?FVector4f(.8f,1,1,1):Kind==2?FVector4f(0,0,1,2):FVector4f(0,0,0,0));
   if(Kind==3){Scene.ShapePlanes={FVector4f(1,0,0,.8f),FVector4f(-1,0,0,.8f),FVector4f(0,1,0,1),FVector4f(0,-1,0,1),FVector4f(0,0,1,1),FVector4f(0,0,-1,1)};Scene.ShapeExtents[0]=FVector4f(0,6,0,3);}
   VamGPUSetScene(H0,MoveTemp(Scene));TArray<FVector4f> R=Points,Spheres={FVector4f(.5f,0,0,.8f)},Starts=Spheres;VamGPUUpdate(H0,MoveTemp(R),MoveTemp(Spheres),MoveTemp(Starts));VamGPUFlushBatch();ENQUEUE_RENDER_COMMAND(VamRigidTestFence)([](FRHICommandListImmediate& Cmd){Cmd.SubmitAndBlockUntilGPUIdle();});FlushRenderingCommands();}
  VamGPUReadDiagnostic(H0,R0,X0);VamGPUReadLoads(H0,L0,Time);FVector F=FVector::ZeroVector;for(const auto& L:L0)F+=L.ForceNewtons;
  TestTrue(FString::Printf(TEXT("Rigid shape %d displaces tissue"),Kind),X0.Num()==5&&X0[4].X<-.02);TestTrue(FString::Printf(TEXT("Rigid shape %d force points into tissue"),Kind),F.X<-.0001);
  AddInfo(FString::Printf(TEXT("COUPLING rigid kind=%d displacement=%s force=%s"),Kind,X0.Num()?*X0[4].ToString():TEXT("absent"),*F.ToString()));
 }

 // Two regions in one handle exercise self contact independently of actor count.
 VamGPUUnregister(A);VamGPUUnregister(B);auto SelfTopology=Build();const auto OtherTopology=Build();
 for(auto T:OtherTopology.Tets){for(int J=0;J<4;++J)T[J]+=5;SelfTopology.Tets.Add(T);}for(auto T:OtherTopology.SkinFaces){for(int J=0;J<4;++J)T[J]+=5;SelfTopology.SkinFaces.Add(T);}for(auto T:OtherTopology.SkinHinges){for(int J=0;J<4;++J)T[J]+=5;SelfTopology.SkinHinges.Add(T);}
 for(auto E:OtherTopology.SurfaceEdges)SelfTopology.SurfaceEdges.Add(FIntPoint(E.X+5,E.Y+5));SelfTopology.EdgeLimits.Append(OtherTopology.EdgeLimits);SelfTopology.Material.Append(OtherTopology.Material);for(int I=0;I<5;++I)SelfTopology.Regions.Add(1);
 auto Self=VamGPURegister(A,MoveTemp(SelfTopology));
 for(int I=0;I<151;++I){TArray<FVector4f> R=Points;const FTransform X(FRotator(0,180,0),FVector(.5-.7*FMath::Min(1.,I/60.),0,0));for(auto P:Points)R.Add(FVector4f(FVector3f(X.TransformPosition(FVector(FVector3f(P)))),P.W));
  FVamGPUContactScene Scene;Scene.WorldId=1;Scene.bSoftCollision=true;VamGPUSetScene(Self,MoveTemp(Scene));TArray<FVector4f> Empty,Starts;VamGPUUpdate(Self,MoveTemp(R),MoveTemp(Empty),MoveTemp(Starts));VamGPUFlushBatch();ENQUEUE_RENDER_COMMAND(VamSelfTestFence)([](FRHICommandListImmediate& Cmd){Cmd.SubmitAndBlockUntilGPUIdle();});FlushRenderingCommands();}
 VamGPUReadDiagnostic(Self,R0,X0);VamGPUReadLoads(Self,L0,Time);FVector SelfForce=FVector::ZeroVector,SelfTorque=FVector::ZeroVector;for(const auto& L:L0){SelfForce+=L.ForceNewtons;SelfTorque+=L.TorqueNewtonMeters;}
 TestTrue(TEXT("Self contact deforms both regions"),X0.Num()==10&&X0[4].X<-.02&&X0[9].X>-.18);TestTrue(TEXT("Self contact has zero external wrench"),SelfForce.Size()<1e-5&&SelfTorque.Size()<1e-5);
 AddInfo(FString::Printf(TEXT("COUPLING self regions=%d force=%s torque=%s"),X0.Num(),*SelfForce.ToString(),*SelfTorque.ToString()));VamGPUUnregister(A);Self.Reset();
 H0.Reset();H1.Reset();FlushRenderingCommands();A->RemoveFromRoot();B->RemoveFromRoot();return true;

}
#endif

#include "ProfilingDebugging/MiscTrace.h"
#include "HAL/IConsoleManager.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Containers/Ticker.h"
#include "Tickable.h"
#include "VamCharacterActor.h"
#include "VamActivePoseComponent.h"
#include "VamMotionComponent.h"
#include "VamCharacterComponent.h"
#include "VamBreastContactComponent.h"
#include "VamRuntimeConfiguration.h"
#include "VamBreastSkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "HAL/PlatformTime.h"
#include "Animation/MeshDeformerGeometryReadback.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "RenderingThread.h"
#include "SkeletalRenderPublic.h"
#include <atomic>
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/BufferArchive.h"
#include "Components/PointLightComponent.h"
#include "ContentStreaming.h"
#include "ShaderCompiler.h"
#include "Engine/StaticMesh.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionFresnel.h"
#include "AssetCompilingManager.h"
#include "ComputeFramework/ComputeFramework.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVamContactBulkTest,"Vam.Breast.ContactBulk",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FVamContactBulkTest::RunTest(const FString&)
{
 auto* P=NewObject<UVamBreastContactProfile>();P->LocalCompressionResistance=.35;
 TestEqual(TEXT("Rest has no correction"),P->CompressionCorrection(10,10),0.);
 TestEqual(TEXT("Expansion has no compressive correction"),P->CompressionCorrection(10,12),0.);
 double Previous=0;
 for(int32 I=0;I<=100;++I){const double J=1-I*.01,C=P->CompressionCorrection(10,10*J);
  TestTrue(TEXT("Compression response finite monotonic"),FMath::IsFinite(C) && C>=Previous);Previous=C;
  TestTrue(TEXT("Scale covariant"),FMath::IsNearlyEqual(P->CompressionCorrection(80,80*J),8*C,1.e-8));}
 TestTrue(TEXT("Intervenes before emergency floor"),P->CompressionCorrection(10,8)>0);
 TestTrue(TEXT("Smooth near rest"),P->CompressionCorrection(10,9.999)<1.e-6);
 P->LocalCompressionResistance=0;
 TestEqual(TEXT("Legacy rest compression unchanged"),P->CompressionCorrection(10,8),0.);
 TestEqual(TEXT("Legacy emergency floor unchanged"),P->CompressionCorrection(10,2),1.5);
 P->Particles.SetNum(3);for(auto& V:P->Particles)V.Side=0;P->BoundaryTriangles={FIntVector(0,1,2)};
 TArray<FVector> Triangle={FVector(0,-3,-2),FVector(0,3,-2),FVector(0,0,3)};
 TestTrue(TEXT("Sphere first touch inside face, all vertices outside radius"),FMath::IsNearlyZero(P->ProbeFront(Triangle,FTransform::Identity,FVector::ZeroVector,1,false,false,0),1.e-5));
 TestTrue(TEXT("Plate first touch clipped face"),FMath::IsNearlyZero(P->ProbeFront(Triangle,FTransform::Identity,FVector::ZeroVector,1,true,false,0),1.e-5));
 const FTransform Frame(FRotator(20,50,10),FVector(100,-25,50));for(auto& V:Triangle)V=Frame.TransformPosition(V);
 TestTrue(TEXT("Contact onset rigid-frame invariant"),FMath::IsNearlyZero(P->ProbeFront(Triangle,Frame,FVector::ZeroVector,1,false,false,0),1.e-5));
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVamContactRuntimeTest,"Vam.Breast.ContactRuntime",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FVamContactRuntimeTest::RunTest(const FString&)
{
 FString Path=TEXT("/Game/VamRuntime/R_f89178069bfcd90f35a9af2b/RC_Runtime");
 FParse::Value(FCommandLine::Get(),TEXT("VamBreastTestConfig="),Path);
 auto* Config=LoadObject<UVamRuntimeConfiguration>(nullptr,*Path);
 if(!TestNotNull(TEXT("Config"),Config)) return false;
 if(FParse::Param(FCommandLine::Get(),TEXT("VamContactTrialBending")))
 {
  Config=DuplicateObject<UVamRuntimeConfiguration>(Config,GetTransientPackage());
  auto* Trial=DuplicateObject<UVamBreastContactProfile>(Config->BreastContact.LoadSynchronous(),Config);Trial->SurfaceBending=.05;Config->BreastContact=Trial;
 }

 const auto IVS=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true).RequiresHitProxies(false).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(true).SetTransactional(false);
 auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&IVS);
 GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
 World->InitializeActorsForPlay(FURL());World->GetWorldSettings()->NotifyBeginPlay();
 auto* A=World->SpawnActor<AVamCharacterActor>();A->BreastContact->bEnabled=false;A->Character->RuntimeConfiguration=Config;A->LoadCharacter();
 const bool GPUVideo=FParse::Param(FCommandLine::Get(),TEXT("VamResidentGPUVideo"));
 if(GPUVideo){A->BreastContact->bUseGPU=true;A->BreastContact->bEnabled=true;}
 FString EvidenceDir;FParse::Value(FCommandLine::Get(),TEXT("VamContactEvidence="),EvidenceDir);
 if(!EvidenceDir.IsEmpty())IFileManager::Get().MakeDirectory(*EvidenceDir,true);
 USceneCaptureComponent2D* Capture=nullptr;
 if(FParse::Param(FCommandLine::Get(),TEXT("VamContactGPU")) || !EvidenceDir.IsEmpty())
 {
  // Controlled geometry comparison: exclude breathing/idle pose changes.
  // The separate native run retains these ordinary runtime inputs.
  A->ActivePose->bBreathing=false;A->ActivePose->bIdle=false;A->ActivePose->bBlink=false;
  auto* Target=NewObject<UTextureRenderTarget2D>(A);Target->ClearColor=FLinearColor(.025f,.035f,.05f,1);Target->InitAutoFormat(EvidenceDir.IsEmpty()?256:768,EvidenceDir.IsEmpty()?256:768);
  Capture=NewObject<USceneCaptureComponent2D>(A);Capture->TextureTarget=Target;Capture->CaptureSource=SCS_FinalColorLDR;Capture->PostProcessSettings.bOverride_AutoExposureMethod=true;Capture->PostProcessSettings.AutoExposureMethod=AEM_Manual;Capture->PostProcessSettings.bOverride_AutoExposureBias=true;Capture->PostProcessSettings.AutoExposureBias=0;Capture->bCaptureEveryFrame=false;Capture->bCaptureOnMovement=false;Capture->RegisterComponent();
  Capture->SetWorldLocationAndRotation(FVector(300,0,100),FRotator(0,180,0));
 }
 auto Tick=[&](){ComputeFramework::TickCompilation(0);++GFrameCounter;FTSTicker::GetCoreTicker().Tick(1.f/60);FTickableGameObject::TickObjects(nullptr,LEVELTICK_All,false,1.f/60);World->Tick(LEVELTICK_All,1.f/60);if(Capture){World->SendAllEndOfFrameUpdates();Capture->CaptureScene();FlushRenderingCommands();}};
 for(int32 I=0;I<15;++I){FlushAsyncLoading();Tick();}
 FVector EvidenceCenter,EvidenceFront,EvidenceUp,EvidenceSide;double EvidenceSize=0;
 if(Capture)
 {
  auto* Breast=CastChecked<UVamBreastSkeletalMeshComponent>(A->Character->Body);Breast->bJiggleEnabled=false;
  if(!TestEqual(TEXT("Bilateral capture frames"),Breast->RestSides.Num(),2))return false;
  const auto& L=Breast->RestSides[0];const auto& R=Breast->RestSides[1];
  const auto& Pose=Breast->GetComponentSpaceTransforms();const FTransform Frame=Pose[L.AnchorBone]*Breast->GetComponentTransform();
  const FVector Center=(Frame.TransformPosition(L.COM)+(Pose[R.AnchorBone]*Breast->GetComponentTransform()).TransformPosition(R.COM))*.5;
  const FVector Front=Frame.GetUnitAxis(EAxis::X),Up=Frame.GetUnitAxis(EAxis::Z);FVector Side=Frame.GetUnitAxis(EAxis::Y);
  if(FVector::DotProduct(Frame.TransformPosition(L.COM)-Center,Side)<0)Side=-Side;
  const double Size=FMath::Max(L.EffectiveRadiusCm,R.EffectiveRadiusCm)*2;
  EvidenceCenter=Center;EvidenceFront=Front;EvidenceUp=Up;EvidenceSide=Side;EvidenceSize=Size;
  const FVector Eye=Center+Front*(Size*5)+Side*(Size*2);
  Capture->FOVAngle=35;Capture->SetWorldLocationAndRotation(Eye,FRotationMatrix::MakeFromXZ((Center-Eye).GetSafeNormal(),Up).Rotator());
  for(int32 I=0;I<2;++I){auto* Light=NewObject<UPointLightComponent>(A);Light->Intensity=I==0?6000:2000;Light->AttenuationRadius=1000;Light->SourceRadius=20;Light->RegisterComponent();Light->SetWorldLocation(Center+Front*100+Up*(I==0?80:10)+Side*(I==0?70:-100));}
  TArray<USkeletalMeshComponent*> Parts;A->GetComponents(Parts);for(auto* Part:Parts)if(Part!=Breast)Part->SetVisibility(false);
  FAssetCompilingManager::Get().FinishAllCompilation();
  if(GShaderCompilingManager)GShaderCompilingManager->FinishAllCompilation();
  IStreamingManager::Get().StreamAllResources(10.f);
 }
 auto SaveImage=[&](const FString& Name){
  if(EvidenceDir.IsEmpty() || !Capture)return;
  if(GShaderCompilingManager)GShaderCompilingManager->FinishAllCompilation();
  auto Export=[&](const FString& Suffix){World->SendAllEndOfFrameUpdates();Capture->CaptureScene();FlushRenderingCommands();FBufferArchive PNG;
   TestTrue(TEXT("Export real UE render PNG"),FImageUtils::ExportRenderTarget2DAsPNG(Capture->TextureTarget,PNG));
   TestTrue(TEXT("Save evidence PNG"),FFileHelper::SaveArrayToFile(PNG,*FPaths::Combine(EvidenceDir,Name+Suffix+TEXT(".png"))));};
  Export(TEXT(""));
  if(Name==TEXT("normal") || Name==TEXT("pressed-left"))
  {
   const auto OriginalFlags=Capture->ShowFlags;
   Capture->ShowFlags.SetDynamicShadows(false);Export(TEXT("-no-shadow"));
   Capture->ShowFlags=OriginalFlags;Capture->ShowFlags.SetWireframe(true);Export(TEXT("-wireframe"));
   Capture->ShowFlags=OriginalFlags;
  }
  if(!Name.StartsWith(TEXT("frame-"))){const FTransform Original=Capture->GetComponentTransform();
   const FVector Eye=EvidenceCenter+EvidenceFront*(EvidenceSize*2)+EvidenceSide*(EvidenceSize*5);
   Capture->SetWorldLocationAndRotation(Eye,FRotationMatrix::MakeFromXZ((EvidenceCenter-Eye).GetSafeNormal(),EvidenceUp).Rotator());Export(TEXT("-side"));Capture->SetWorldTransform(Original);}
 };
 int32 SnapshotIndex=0;
 auto ReadGPU=[&]() -> TArray<FVector3f> {
  if(!Capture)return {};
  struct FResult { std::atomic<bool> Ready{false};TArray<FVector3f> Positions; };
  auto Result=MakeShared<FResult,ESPMode::ThreadSafe>();auto Request=MakeUnique<FMeshDeformerGeometryReadbackRequest>();
  Request->VertexDataArraysCallback_AnyThread=[Result](const FMeshDeformerGeometryReadbackVertexDataArrays& Data){Result->Positions=Data.Positions;Result->Ready.store(true);};
  TestTrue(TEXT("GPU readback request accepted"),A->Character->Body->RequestReadbackRenderGeometry(MoveTemp(Request)));
  for(int32 I=0;I<60 && !Result->Ready.load();++I)Tick();
  TestTrue(TEXT("GPU readback returned vertices"),Result->Ready.load() && !Result->Positions.IsEmpty());
  FString SnapshotDir;
  if(Result->Ready.load() && FParse::Value(FCommandLine::Get(),TEXT("VamContactSnapshots="),SnapshotDir))
  {
   IFileManager::Get().MakeDirectory(*SnapshotDir,true);
   const auto Bytes=MakeArrayView(reinterpret_cast<const uint8*>(Result->Positions.GetData()),Result->Positions.Num()*sizeof(FVector3f));
   TestTrue(TEXT("Save GPU comparison positions"),FFileHelper::SaveArrayToFile(Bytes,*FPaths::Combine(SnapshotDir,FString::Printf(TEXT("gpu-%02d.bin"),SnapshotIndex++))));
  }
  return Result->Ready.load()?Result->Positions:TArray<FVector3f>();
 };
 const auto Materials=A->Character->Body->GetMaterials();
 auto CheckMaterials=[&](){TestTrue(TEXT("Body material slots preserved"),Materials==A->Character->Body->GetMaterials());};
 auto CheckIdle=[&](){TestEqual(TEXT("Idle has zero solvers"),A->BreastContact->GetActiveSolverCount(),0);TestFalse(TEXT("Native skin restored"),A->Character->Body->HasMeshDeformer());CheckMaterials();};
 auto CheckPress=[&](int32 Side){
  TestTrue(TEXT("Contact stays enabled"),A->BreastContact->bEnabled);
  TestEqual(TEXT("One contact solver"),A->BreastContact->GetActiveSolverCount(),1);
  TestTrue(TEXT("GPU deformer assigned"),A->Character->Body->HasMeshDeformer());
  TestTrue(TEXT("Cage displacement exceeds 0.5 cm"),A->BreastContact->GetMaxContactResidualCm()>.5);
  TestTrue(TEXT("Bound skin displacement exceeds 0.3 cm"),A->BreastContact->GetBoundSurfaceResidualCm()[Side]>.3);
  TestEqual(TEXT("Two volume diagnostics"),A->BreastContact->VolumeState.Num(),2);
  for(const auto& V:A->BreastContact->VolumeState){TestEqual(TEXT("No inverted tets"),V.InvertedTetrahedra,0);TestTrue(TEXT("Finite volume within 5 percent"),FMath::IsFinite(V.RelativeVolumeError) && FMath::Abs(V.RelativeVolumeError)<.05);}
  TestTrue(TEXT("Local compression stays supported"),A->BreastContact->VolumeState.IsEmpty() || A->BreastContact->VolumeState[Side].MinimumTetRatio>.15);
  if(!A->BreastContact->VolumeState.IsEmpty())TestTrue(TEXT("No extreme surface stretch"),A->BreastContact->VolumeState[Side].MaximumSurfaceStretch<1.6);
  CheckMaterials();
 };
 auto Phase=[&](const TCHAR* Name){const FString Region=FString(TEXT("Contact_"))+Name;TRACE_BEGIN_REGION(*Region);double Start=FPlatformTime::Seconds();for(int32 I=0;I<90;++I)Tick();TRACE_END_REGION(*Region);AddInfo(FString::Printf(TEXT("CONTACT_PHASE %s frame_ms=%.3f %s"),Name,(FPlatformTime::Seconds()-Start)*1000/90,*A->BreastContact->Diagnostics()));};
 if(FParse::Param(FCommandLine::Get(),TEXT("VamContactDisabledBenchmark")))
 {
  auto* Breast=CastChecked<UVamBreastSkeletalMeshComponent>(A->Character->Body);Breast->bJiggleEnabled=true;
  A->SetBreastContactEnabled(true);A->BreastContact->SetDebugPress(0,.2f);
  for(int I=0;I<30;++I)Tick();
  TestTrue(TEXT("Toggle on creates solver"),A->BreastContact->GetActiveSolverCount()>0);
  A->SetBreastContactEnabled(false);CheckIdle();
  A->BreastContact->ResetContact();TestFalse(TEXT("Reset preserves disabled"),A->IsBreastContactEnabled());
  TestTrue(TEXT("Toggle preserves breast Jiggle"),Breast->bJiggleEnabled);
  for(int I=0;I<120;++I)Tick();
  TArray<double> Samples;
  for(int I=0;I<600;++I){const double Start=FPlatformTime::Seconds();Tick();Samples.Add((FPlatformTime::Seconds()-Start)*1000);}
  double Sum=0;for(double V:Samples)Sum+=V;Samples.Sort();
  AddInfo(FString::Printf(TEXT("CONTACT_DISABLED_BENCH samples=600 mean_ms=%.3f p50_ms=%.3f p95_ms=%.3f p99_ms=%.3f max_ms=%.3f Jiggle=on solvers=%d"),Sum/600,Samples[300],Samples[570],Samples[594],Samples.Last(),A->BreastContact->GetActiveSolverCount()));
  CheckIdle();World->EndPlay(EEndPlayReason::Quit);A->Destroy();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);return true;
 }
 if(!GPUVideo){
 A->BreastContact->bDebugPlaten=true; // Retain the sharp-platen regression.
 Phase(TEXT("disabled_initial"));A->BreastContact->bWorldCollision=false;A->BreastContact->bEnabled=true;Phase(TEXT("idle"));CheckIdle();SaveImage(TEXT("normal"));
 TArray<FVector3f> GPUBase;
 if(Capture){CastChecked<UVamBreastSkeletalMeshComponent>(A->Character->Body)->bJiggleEnabled=false;A->BreastContact->SetDebugPress(0,0);Phase(TEXT("gpu_zero_pressure"));TArray<FFinalSkinVertex> NativeVertices;
 // Test-only CPU reference; ordinary characters remain entirely native GPU-skinned.
 A->Character->Body->GetCPUSkinnedVertices(NativeVertices,0);
 GPUBase=ReadGPU();
 if(const auto* CP=Config->BreastContact.LoadSynchronous())if(CP->bResidualOnlySurface){
  TestEqual(TEXT("Zero-contact GPU/native vertex count"),GPUBase.Num(),NativeVertices.Num());
  double Error=0;if(GPUBase.Num()==NativeVertices.Num())for(int32 V=0;V<GPUBase.Num();++V)if(CP->SurfaceMask[V]>.01)Error=FMath::Max(Error,double((GPUBase[V]-NativeVertices[V].Position).Size()));
  AddInfo(FString::Printf(TEXT("ZERO_CONTACT_GPU_NATIVE max_error_cm=%.8f"),Error));
  TestTrue(TEXT("Direct residual has no quantized rest-position dent"),Error<.005);
 }
 TestTrue(TEXT("Zero-pressure cage stays within 0.5 mm of animated baseline"),A->BreastContact->GetMaxContactResidualCm()<.05);SaveImage(TEXT("zero-pressure"));}
 double UniformNippleStrain=-1;
 if(const auto* CP=Config->BreastContact.LoadSynchronous())if(CP->Particles.ContainsByPredicate([](const FVamBreastContactParticle& P){return P.NippleSupport>.25;}))
 {
  A->BreastContact->NippleShapePreservationScale=0;A->BreastContact->SetDebugPress(0,.2f);Phase(TEXT("uniform_nipple_press"));CheckPress(0);SaveImage(TEXT("uniform-nipple"));
  if(A->BreastContact->VolumeState.Num()==2){UniformNippleStrain=A->BreastContact->VolumeState[0].NippleShapeRmsStrain;TestTrue(TEXT("Nipple source region has shape constraints"),A->BreastContact->VolumeState[0].NippleShapePairCount>=3);}
  A->BreastContact->NippleShapePreservationScale=1;A->BreastContact->ResetContact();
 }
 A->BreastContact->SetDebugPress(0,.2f);Phase(TEXT("press_left"));CheckPress(0);SaveImage(TEXT("pressed-left"));
 if(UniformNippleStrain>=0 && A->BreastContact->VolumeState.Num()==2){const double Refined=A->BreastContact->VolumeState[0].NippleShapeRmsStrain;
  TestTrue(TEXT("Local nipple material reduces shape strain"),Refined<UniformNippleStrain*.9);
  AddInfo(FString::Printf(TEXT("NIPPLE_MATERIAL uniform_rms=%.6f refined_rms=%.6f"),UniformNippleStrain,Refined));}

 if(Capture){const auto Pressed=ReadGPU();double MaxDelta=0;if(Pressed.Num()==GPUBase.Num())for(int32 I=0;I<Pressed.Num();++I)MaxDelta=FMath::Max(MaxDelta,double((Pressed[I]-GPUBase[I]).Size()));TestTrue(TEXT("GPU surface changed under press"),MaxDelta>.3);AddInfo(FString::Printf(TEXT("GPU_PRESS vertices=%d max_delta_cm=%.4f"),Pressed.Num(),MaxDelta));}
 if(Capture){const auto Pressed=ReadGPU();const auto* CP=Config->BreastContact.LoadSynchronous();double Area=0,ChangedArea=0,Mean=0,MaxBody=0;
  if(CP && Pressed.Num()==GPUBase.Num())for(const auto& T:CP->MeasurementTriangles){if(CP->Particles[CP->SurfaceParents[T.X][0]].Side!=0)continue;
   if(FMath::Max3(CP->SurfaceNippleSupport[T.X],CP->SurfaceNippleSupport[T.Y],CP->SurfaceNippleSupport[T.Z])>.1)continue;
   const FVector X(GPUBase[T.X]),Y(GPUBase[T.Y]),Z(GPUBase[T.Z]);const double A0=FVector::CrossProduct(Y-X,Z-X).Size()*.5;
   const double D=((Pressed[T.X]-GPUBase[T.X]).Size()+(Pressed[T.Y]-GPUBase[T.Y]).Size()+(Pressed[T.Z]-GPUBase[T.Z]).Size())/3;
   Area+=A0;Mean+=A0*D;MaxBody=FMath::Max(MaxBody,D);if(D>.3)ChangedArea+=A0;
  }
  TestTrue(TEXT("Body-only measurement has area"),Area>1);
  TestTrue(TEXT("At least 10 percent of non-nipple surface moves over 3 mm"),ChangedArea>Area*.1);
  AddInfo(FString::Printf(TEXT("BODY_SURFACE area_cm2=%.4f changed_fraction=%.4f mean_delta_cm=%.4f max_body_delta_cm=%.4f"),Area,ChangedArea/FMath::Max(1.,Area),Mean/FMath::Max(1.,Area),MaxBody));
 }
 A->BreastContact->ResetContact();A->BreastContact->bDebugPlaten=false;A->BreastContact->DebugPressOffset=FVector2D::ZeroVector;A->BreastContact->SetDebugPress(0,.2f);Phase(TEXT("central_sphere"));CheckPress(0);SaveImage(TEXT("central-sphere"));
 A->BreastContact->ResetContact();A->BreastContact->bDebugPlaten=false;A->BreastContact->DebugPressOffset=FVector2D(0,.45);A->BreastContact->SetDebugPress(0,.2f);Phase(TEXT("off_nipple_sphere"));CheckPress(0);SaveImage(TEXT("off-nipple-sphere"));
 A->BreastContact->ResetContact();A->BreastContact->bDebugPlaten=true;A->BreastContact->DebugPressOffset=FVector2D::ZeroVector;
 A->BreastContact->SetDebugPress(1,.2f);Phase(TEXT("press_right"));CheckPress(1);SaveImage(TEXT("pressed-right"));
 A->BreastContact->SetDebugPress(INDEX_NONE,0);Phase(TEXT("release"));CheckIdle();SaveImage(TEXT("released"));
 A->BreastContact->bEnabled=false;Phase(TEXT("disabled_after"));CheckIdle();
 A->BreastContact->ResetContact();A->SetBreastContactEnabled(true);A->SetActorLocation(FVector(150,220,100));A->BreastContact->SetDebugPress(0,.2f);Phase(TEXT("translated_press"));CheckPress(0);
 A->BreastContact->ResetContact();A->FindComponentByClass<UVamMotionComponent>()->TeleportTo(FTransform(FRotator(0,90,0),A->GetActorLocation()),World->GetTimeSeconds());Phase(TEXT("rotated_press"));CheckPress(0);
 }
 if(Capture)
 {
  A->FindComponentByClass<UVamMotionComponent>()->TeleportTo(FTransform::Identity,World->GetTimeSeconds());
  A->BreastContact->SetDebugPress(INDEX_NONE,0);Phase(TEXT("video_neutral"));
  
  // A real movable rigid mesh drives the ordinary WORLD collision path. No
  // invisible debug press source participates in these frames.
  auto* ProbeActor=World->SpawnActor<AActor>();
  auto* Probe=NewObject<UStaticMeshComponent>(ProbeActor);ProbeActor->SetRootComponent(Probe);
  auto* SphereMesh=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Sphere.Sphere"));
  if(!TestNotNull(TEXT("World press mesh"),SphereMesh))return false;
  Probe->SetStaticMesh(SphereMesh);Probe->SetMobility(EComponentMobility::Movable);
  Probe->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);Probe->SetCollisionObjectType(ECC_WorldDynamic);
  Probe->SetCollisionResponseToAllChannels(ECR_Block);Probe->SetSimulatePhysics(false);Probe->SetCastShadow(false);
  auto* Glass=NewObject<UMaterial>(ProbeActor);Glass->BlendMode=BLEND_Translucent;Glass->TranslucencyLightingMode=TLM_SurfacePerPixelLighting;
  auto* Color=NewObject<UMaterialExpressionConstant3Vector>(Glass);Color->Constant=FLinearColor(.12f,.55f,.7f);
  Glass->GetExpressionCollection().AddExpression(Color);Glass->GetEditorOnlyData()->BaseColor.Connect(0,Color);
  auto Scalar=[&](FScalarMaterialInput& Input,float Value){auto* E=NewObject<UMaterialExpressionConstant>(Glass);E->R=Value;Glass->GetExpressionCollection().AddExpression(E);Input.Connect(0,E);};
  auto* Rim=NewObject<UMaterialExpressionFresnel>(Glass);Rim->Exponent=3;Rim->BaseReflectFraction=.12f;Glass->GetExpressionCollection().AddExpression(Rim);Glass->GetEditorOnlyData()->Opacity.Connect(0,Rim);Scalar(Glass->GetEditorOnlyData()->Roughness,.08f);Scalar(Glass->GetEditorOnlyData()->Specular,.9f);
  // No refraction: the probe must not optically magnify or hide skin deformation.
  Glass->PostEditChange();Probe->SetMaterial(0,Glass);Probe->RegisterComponent();
  auto* Breast=CastChecked<UVamBreastSkeletalMeshComponent>(A->Character->Body);
  const auto& Rest=Breast->RestSides[0];const FTransform Frame=Breast->GetComponentSpaceTransforms()[Rest.AnchorBone]*Breast->GetComponentTransform();
  const double Radius=Rest.EffectiveRadiusCm*.55;FVector Center=Rest.COM;
  double Front=-DBL_MAX,BodyFront=-DBL_MAX;
  const auto* CP=Config->BreastContact.LoadSynchronous();
  const FTransform LocalFrame=Breast->GetComponentSpaceTransforms()[Rest.AnchorBone];
  for(const auto& P:CP->Particles)if(P.Side==0 && !P.bKinematic){const FVector Q=LocalFrame.InverseTransformPosition(P.Rest);const double R2=FMath::Square(Q.Y-Center.Y)+FMath::Square(Q.Z-Center.Z);
   if(R2>=Radius*Radius)continue;const double X=Q.X-Radius+FMath::Sqrt(Radius*Radius-R2);Front=FMath::Max(Front,X);if(P.NippleSupport<.1)BodyFront=FMath::Max(BodyFront,X);}
  TArray<FVector> RestPoints;for(const auto& P:CP->Particles)RestPoints.Add(P.Rest);
  Front=CP->ProbeFront(RestPoints,LocalFrame,Center,Radius,false,false,0);BodyFront=CP->ProbeFront(RestPoints,LocalFrame,Center,Radius,false,true,0);
  TestTrue(TEXT("World sphere contact support finite"),Front>-DBL_MAX && BodyFront>-DBL_MAX);
  const double Stroke=.2*Rest.EffectiveDepthCm,Gap=Radius*.5;
  Probe->SetWorldScale3D(FVector(Radius/SphereMesh->GetBounds().BoxExtent.X));
  Center.X=Front+Radius+Gap;Probe->SetWorldLocation(Frame.TransformPosition(Center));
  A->BreastContact->bWorldCollision=true;A->BreastContact->PressSpheres.Reset();
  FAssetCompilingManager::Get().FinishAllCompilation();if(GShaderCompilingManager)GShaderCompilingManager->FinishAllCompilation();
  TArray<double> MovingContactMs,HeldContactMs;
  for(int32 I=0;I<360;++I){
   if(I==60)TRACE_BEGIN_REGION(TEXT("Contact_world_moving"));
   if(I==150){TRACE_END_REGION(TEXT("Contact_world_moving"));TRACE_BEGIN_REGION(TEXT("Contact_world_held"));}
   if(I==240){TRACE_END_REGION(TEXT("Contact_world_held"));TRACE_BEGIN_REGION(TEXT("Contact_world_release"));}
   if(I==330)TRACE_END_REGION(TEXT("Contact_world_release"));
   double T=I<60?0:I<150?(I-60)/90.:I<240?1:I<330?1-(I-240)/90.:0;T=T*T*(3-2*T);
   Center.X=Front+Radius+Gap-T*(Gap+Stroke);Probe->SetWorldLocation(Frame.TransformPosition(Center));
   const double StepStart=FPlatformTime::Seconds();Tick();const double StepMs=(FPlatformTime::Seconds()-StepStart)*1000;
   if((I>=60 && I<150) || (I>=240 && I<330))MovingContactMs.Add(StepMs);else if(I>=150 && I<240)HeldContactMs.Add(StepMs);
   if(I==30)SaveImage(TEXT("glass-normal"));if(I==220){SaveImage(TEXT("glass-pressed"));if(!GPUVideo)CheckPress(0);else TestTrue(TEXT("Resident GPU video backend active"),A->BreastContact->Status.Contains(TEXT("GPU resident")));AddInfo(TEXT("WORLD_GLASS_CONTACT: kinematic StaticMesh sphere, no debug press sources"));
    AddInfo(A->BreastContact->Diagnostics());const auto GPU=ReadGPU();double Penetration=0;
    for(int32 V=0;V<GPU.Num();++V)if(CP->SurfaceMask.IsValidIndex(V) && CP->SurfaceMask[V]>.9){const FVector WP=Breast->GetComponentTransform().TransformPosition(FVector(GPU[V]));Penetration=FMath::Max(Penetration,Radius-FVector::Distance(WP,Probe->GetComponentLocation()));}
    AddInfo(FString::Printf(TEXT("WORLD_GLASS_GPU max_vertex_penetration_cm=%.6f"),Penetration));
    TestTrue(TEXT("World sphere render penetration bounded to 2 mm"),Penetration<.2);}
   if(I==359)SaveImage(TEXT("glass-released"));if(I%2==0)SaveImage(FString::Printf(TEXT("frame-%04d"),I/2));}
  auto ReportContactTiming=[&](const TCHAR* Name,TArray<double>& Samples){double Sum=0;for(double V:Samples)Sum+=V;Samples.Sort();AddInfo(FString::Printf(TEXT("CONTACT_TRAJECTORY %s count=%d mean_ms=%.3f p95_ms=%.3f max_ms=%.3f"),Name,Samples.Num(),Sum/Samples.Num(),Samples[FMath::Min(Samples.Num()-1,FMath::FloorToInt(Samples.Num()*.95))],Samples.Last()));};
  ReportContactTiming(TEXT("moving"),MovingContactMs);ReportContactTiming(TEXT("held"),HeldContactMs);
  if(FParse::Param(FCommandLine::Get(),TEXT("VamContactPerformanceAB")))
  {
   auto* Bounds=IConsoleManager::Get().FindConsoleVariable(TEXT("vam.Contact.Broadphase"));
   auto* Dirty=IConsoleManager::Get().FindConsoleVariable(TEXT("vam.Contact.DirtyConstraints"));
   auto* Cache=IConsoleManager::Get().FindConsoleVariable(TEXT("vam.Contact.ExactQueryCache"));
   const int OldBounds=Bounds->GetInt(),OldDirty=Dirty->GetInt(),OldCache=Cache->GetInt();
   for(int Run=0;Run<4;++Run)
   {
    const int Optimized=Run==1 || Run==2;
    Bounds->Set(Optimized,ECVF_SetByCode);Dirty->Set(Optimized,ECVF_SetByCode);Cache->Set(0,ECVF_SetByCode);
    A->BreastContact->ResetContact();Center.X=Front+Radius+Gap;Probe->SetWorldLocation(Frame.TransformPosition(Center));
    for(int I=0;I<60;++I)Tick();
    TArray<double> Moving,Held;
    for(int I=0;I<180;++I)
    {
     double T=I<60?I/59.:I<120?1.:1-(I-120)/59.;T=T*T*(3-2*T);
     Center.X=Front+Radius+Gap-T*(Gap+Stroke);Probe->SetWorldLocation(Frame.TransformPosition(Center));
     const double Begin=FPlatformTime::Seconds();Tick();const double Ms=(FPlatformTime::Seconds()-Begin)*1000;
     (I>=60 && I<120?Held:Moving).Add(Ms);
     if(I==119){CheckPress(0);ReadGPU();}
    }
    ReportContactTiming(*FString::Printf(TEXT("AB%d_%s_moving"),Run,Optimized?TEXT("optimized"):TEXT("baseline")),Moving);
    ReportContactTiming(*FString::Printf(TEXT("AB%d_%s_held"),Run,Optimized?TEXT("optimized"):TEXT("baseline")),Held);
   }
   Bounds->Set(OldBounds,ECVF_SetByCode);Dirty->Set(OldDirty,ECVF_SetByCode);Cache->Set(OldCache,ECVF_SetByCode);
  }
  Probe->SetCollisionEnabled(ECollisionEnabled::NoCollision);ProbeActor->Destroy();A->BreastContact->bWorldCollision=false;Phase(TEXT("world_probe_removed"));CheckIdle();
 }
 A->BreastContact->bEnabled=false;Tick();
 World->EndPlay(EEndPlayReason::Quit);A->Destroy();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);
 return true;
}
#endif

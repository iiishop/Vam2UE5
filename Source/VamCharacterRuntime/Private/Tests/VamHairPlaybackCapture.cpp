#include "CoreMinimal.h"
#if WITH_EDITOR
#include "VamCharacterActor.h"
#include "VamCharacterComponent.h"
#include "VamActivePoseComponent.h"
#include "VamShapeAnimInstance.h"
#include "VamLegSkeletalMeshComponent.h"
#include "VamBreastContactComponent.h"
#include "VamBodyContactResponseComponent.h"
#include "VamHairComponent.h"
#include "Tests/VamHairCaptureAudit.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/GameViewportClient.h"
#include "UnrealClient.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "ShaderCompiler.h"
#include "HAL/IConsoleManager.h"
#include "Containers/Ticker.h"
#include "Misc/FileHelper.h"
#include "Misc/App.h"
#include "ImageUtils.h"
#include "Serialization/JsonSerializer.h"
namespace VamHairPlayback
{
static FTransform Transform(const TSharedPtr<FJsonValue>& V){FMatrix M;const auto& A=V->AsArray();for(int I=0;I<4;++I)for(int J=0;J<4;++J)M.M[I][J]=A[4*I+J]->AsNumber();return FTransform(M);}
struct FTake
{
 TWeakObjectPtr<UWorld> World;TWeakObjectPtr<AVamCharacterActor> Actor;TWeakObjectPtr<ACameraActor> Camera;
 TWeakObjectPtr<AActor> Backdrop;TWeakObjectPtr<UDirectionalLightComponent> Sun,Fill;
 TArray<TSharedPtr<FJsonValue>> Samples,Audits;FString Directory;int Frame=-180,Count=1440,Written=0;bool Pending=false,Close=true,Rest=false,NoCollision=false,NoGravity=false,Springs=false,Dense=false;FDelegateHandle ScreenshotHandle;FVector Front,Right;
 bool Tick(float)
 {
  auto* W=World.Get();auto* A=Actor.Get();if(!W||!A)return false;
  auto* B=Cast<UVamLegSkeletalMeshComponent>(A->Character->Body);if(!B||!A->Hair->Groom||(GShaderCompilingManager&&GShaderCompilingManager->IsCompiling()))return true;
  if(Pending)return true;
  if(Frame==-180){if(Springs||Dense){for(auto& P:A->Hair->Groom->GroomAsset->GetHairGroupsPhysics()){if(Springs)P.SolverSettings.NiagaraSolver=EGroomNiagaraSolvers::AngularSprings;if(Dense){P.SolverSettings.SubSteps=10;P.SolverSettings.IterationCount=32;}}A->Hair->Groom->ReleaseHairSimulation();A->Hair->Groom->UpdateHairSimulation();}if(NoCollision)A->Hair->Groom->SetPhysicsAsset(nullptr);if(NoGravity)for(auto& P:A->Hair->Groom->GroomAsset->GetHairGroupsPhysics())P.ExternalForces.GravityVector=FVector::ZeroVector;A->Hair->Groom->ResetSimulation();}
  if(Frame>=Count)
  {
   auto R=MakeShared<FJsonObject>();R->SetStringField(TEXT("world"),W->WorldType==EWorldType::PIE?TEXT("PIE"):TEXT("Game"));R->SetStringField(TEXT("actor"),A->GetClass()->GetPathName());R->SetStringField(TEXT("hair"),A->Hair->Status);R->SetBoolField(TEXT("glute_jiggle"),CastChecked<UVamLegSkeletalMeshComponent>(A->Character->Body)->bGluteJiggleEnabled);R->SetBoolField(TEXT("leg_jiggle"),CastChecked<UVamLegSkeletalMeshComponent>(A->Character->Body)->bLegJiggleEnabled);R->SetNumberField(TEXT("frames"),Written);R->SetNumberField(TEXT("simulation_dt"),1./60.);R->SetNumberField(TEXT("video_fps"),30);R->SetBoolField(TEXT("rest_control"),Rest);R->SetArrayField(TEXT("gpu_samples"),Audits);FString Text;FJsonSerializer::Serialize(R,TJsonWriterFactory<>::Create(&Text));FFileHelper::SaveStringToFile(Text,*(Directory/TEXT("capture.json")));UGameViewportClient::OnScreenshotCaptured().Remove(ScreenshotHandle);FApp::SetUseFixedTimeStep(false);GEngine->Exec(W,TEXT("quit"));return false;
  }
  auto* Anim=CastChecked<UVamShapeAnimInstance>(B->GetAnimInstance());const auto& Ref=B->GetSkeletalMeshAsset()->GetRefSkeleton();const auto Reference=A->Character->GetShapeReferencePose();
  const int Index=FMath::Clamp(FMath::Max(Frame,0)/2,0,Samples.Num()-1);const auto L=Samples[Index]->AsObject(),R=Samples[FMath::Min(Index+1,Samples.Num()-1)]->AsObject();const float Alpha=Frame>0&&Frame%2?.5f:0.f;
  FTransform WorldPose;WorldPose.Blend(Transform(L->TryGetField(TEXT("actor"))),Transform(R->TryGetField(TEXT("actor"))),Alpha);A->SetActorTransform(WorldPose);
  const auto& LB=L->GetArrayField(TEXT("bones"));const auto& RB=R->GetArrayField(TEXT("bones"));TArray<FTransform> CS;CS.SetNum(LB.Num());for(int I=0;I<CS.Num();++I)CS[I].Blend(Transform(LB[I]),Transform(RB[I]),Alpha);
  TSet<int> Main;Main.Add(0);for(const auto& J:A->Character->RigProfile->Joints)Main.Add(Ref.FindBoneIndex(J.Bone));
  for(int I:Main)if(I>=0&&I<CS.Num()){const int P=Ref.GetParentIndex(I);const auto Local=P<0?CS[I]:CS[I].GetRelativeTransform(CS[P]);const FQuat Q=(Local.GetRotation()*Reference[I].GetRotation().Inverse()).GetNormalized();Anim->GeometricDebugBones.Add(I);A->Character->SetDebugBoneOffset(I,FTransform(Q,Local.GetTranslation()-Reference[I].GetTranslation()));}
  B->bGluteJiggleEnabled=B->bLegJiggleEnabled=true;B->bJiggleEnabled=false;if(Rest&&Frame==-180){A->Hair->Groom->SimulationSettings.bOverrideSettings=true;A->Hair->Groom->SimulationSettings.SolverSettings.bEnableSimulation=true;A->Hair->Groom->SetEnableSimulation(false);}
  const FVector F=WorldPose.TransformVectorNoScale(Front),Rt=WorldPose.TransformVectorNoScale(Right),Up=FVector::UpVector;const int Head=Ref.FindBoneIndex(TEXT("head"));const FVector Look=WorldPose.TransformPosition(Close?CS[Head].GetLocation()+Up*5:FVector(0,0,85));const FVector Eye=Look-F*(Close?100:340)+Rt*(Close?30:75)+Up*(Close?6:65);Camera->SetActorLocationAndRotation(Eye,(Look-Eye).Rotation());Sun->SetWorldRotation((F-Rt-Up).Rotation());Fill->SetWorldRotation((-F+Rt-Up*.3).Rotation());Backdrop->SetActorLocation(WorldPose.GetLocation()+F*600+Up*300);Backdrop->SetActorRotation(FRotationMatrix::MakeFromYZ(F,Up).Rotator());
  if(Frame>=0&&Frame%2==0){Pending=true;FScreenshotRequest::RequestScreenshot(false);}
  ++Frame;return true;
 }
};
FAutoConsoleCommandWithWorldAndArgs Command(TEXT("vam.HairCapture"),TEXT("Output, BP class, existing locomotion JSON, fine/coarse/rest; optional preview/full-body."),FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args,UWorld* W)
{
 if(Args.Num()<4||!W||!W->IsGameWorld()||!GEngine->GameViewport)return;
 FString Text;TSharedPtr<FJsonObject> Source;if(!FFileHelper::LoadFileToString(Text,*Args[2])||!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Source))return;
 auto* Class=LoadClass<AVamCharacterActor>(nullptr,*Args[1]);if(!Class)return;auto P=MakeShared<FTake>();P->World=W;P->Directory=Args[0];P->Samples=Source->GetArrayField(TEXT("samples"));const auto& Front=Source->GetArrayField(TEXT("target_front"));P->Front=FVector(Front[0]->AsNumber(),Front[1]->AsNumber(),Front[2]->AsNumber());P->Right=FVector::CrossProduct(FVector::UpVector,P->Front).GetSafeNormal();P->Rest=Args[3]==TEXT("rest");P->Springs=Args.Contains(TEXT("springs"));P->Dense=Args.Contains(TEXT("dense"));P->NoCollision=Args.Contains(TEXT("no-collision"));P->NoGravity=Args.Contains(TEXT("no-gravity"));P->Count=Args.Contains(TEXT("preview"))?360:P->Samples.Num()*2;P->Close=!Args.Contains(TEXT("full-body"));IFileManager::Get().MakeDirectory(*(P->Directory/TEXT("video")),true);
 auto* A=W->SpawnActor<AVamCharacterActor>(Class);P->Actor=A;A->BreastContact->SetContactEnabled(false);A->BodyContactResponse->bEnabled=false;A->ActivePose->bBreathing=A->ActivePose->bIdle=A->ActivePose->bBlink=false;A->Hair->SetHairQuality(Args[3]==TEXT("coarse")?EVamHairQuality::Coarse:EVamHairQuality::Fine);A->LoadCharacter();
 auto* Camera=W->SpawnActor<ACameraActor>();P->Camera=Camera;auto* C=Camera->GetCameraComponent();C->FieldOfView=60;C->PostProcessSettings.bOverride_AutoExposureMethod=true;C->PostProcessSettings.AutoExposureMethod=AEM_Manual;C->PostProcessSettings.bOverride_AutoExposureBias=true;C->PostProcessSettings.AutoExposureBias=1;C->PostProcessSettings.bOverride_AutoExposureApplyPhysicalCameraExposure=true;C->PostProcessSettings.AutoExposureApplyPhysicalCameraExposure=false;C->PostProcessSettings.bOverride_MotionBlurAmount=true;C->PostProcessSettings.MotionBlurAmount=0;if(auto* PC=W->GetFirstPlayerController()){PC->SetViewTarget(Camera);if(auto Pawn=PC->GetPawn())Pawn->SetActorHiddenInGame(true);}
 auto* Material=NewObject<UMaterial>(GetTransientPackage());Material->SetShadingModel(MSM_Unlit);auto* Color=NewObject<UMaterialExpressionConstant3Vector>(Material);Color->Constant=FLinearColor(.22,.25,.3);Material->GetExpressionCollection().AddExpression(Color);Material->GetEditorOnlyData()->EmissiveColor.Expression=Color;Material->PostEditChange();Material->ForceRecompileForRendering();
 auto Box=[&](FVector Location,FVector Size){auto* Prop=W->SpawnActor<AActor>();auto* M=NewObject<UStaticMeshComponent>(Prop);Prop->SetRootComponent(M);M->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));M->SetMaterial(0,Material);M->SetCollisionEnabled(ECollisionEnabled::NoCollision);M->RegisterComponent();Prop->SetActorLocation(Location);Prop->SetActorScale3D(Size/100);return Prop;};P->Backdrop=Box(FVector(600,0,300),FVector(3000,5,3000));Box(FVector(1000,0,-12),FVector(6000,6000,10));
 for(int I=0;I<2;++I){auto* Prop=W->SpawnActor<AActor>();auto* Light=NewObject<UDirectionalLightComponent>(Prop);Prop->SetRootComponent(Light);Light->Intensity=I==0?3:.7;Light->RegisterComponent();if(I==0)P->Sun=Light;else P->Fill=Light;}
 P->ScreenshotHandle=UGameViewportClient::OnScreenshotCaptured().AddLambda([P](int Width,int Height,const TArray<FColor>& Pixels){if(!P->Pending)return;TArray<uint8> PNG;FImageUtils::CompressImageArray(Width,Height,Pixels,PNG);FFileHelper::SaveArrayToFile(PNG,*(P->Directory/TEXT("video")/FString::Printf(TEXT("frame-%04d.png"),P->Written)));if(P->Written%30==0){auto R=VamCaptureHairAudit(P->Actor.Get());R->SetNumberField(TEXT("time"),P->Written/30.);P->Audits.Add(MakeShared<FJsonValueObject>(R));}++P->Written;P->Pending=false;});
 GEngine->Exec(W,TEXT("r.SetRes 1280x720w"));GEngine->Exec(W,TEXT("r.VSync 0"));GEngine->Exec(W,TEXT("t.MaxFPS 0"));GEngine->Exec(W,TEXT("r.ScreenshotDelegate 1"));FApp::SetFixedDeltaTime(1./60.);FApp::SetUseFixedTimeStep(true);FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([P](float Dt){return P->Tick(Dt);}));
}));
}
#endif




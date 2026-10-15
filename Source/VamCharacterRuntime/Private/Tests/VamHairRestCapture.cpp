#include "CoreMinimal.h"
#if WITH_EDITOR
#include "VamCharacterActor.h"
#include "VamCharacterComponent.h"
#include "VamActivePoseComponent.h"
#include "VamLegSkeletalMeshComponent.h"
#include "VamBreastContactComponent.h"
#include "VamBodyContactResponseComponent.h"
#include "VamHairComponent.h"
#include "Tests/VamHairCaptureAudit.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "UnrealClient.h"
#include "ImageUtils.h"
#include "ShaderCompiler.h"
#include "HAL/IConsoleManager.h"
#include "Containers/Ticker.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonSerializer.h"

namespace VamHairRestCapture {
FVector V(const TSharedPtr<FJsonObject>& O,const TCHAR* Key){const auto& A=O->GetArrayField(Key);return FVector(A[0]->AsNumber(),A[1]->AsNumber(),A[2]->AsNumber());}
struct FTake {
 TWeakObjectPtr<UWorld> World;TWeakObjectPtr<AVamCharacterActor> Actor;TWeakObjectPtr<ACameraActor> Camera;
 TWeakObjectPtr<AActor> Backdrop;TWeakObjectPtr<UDirectionalLightComponent> Key,Fill;FString Directory;TArray<TSharedPtr<FJsonValue>> Views,Results;
 int32 Index=0,Frame=0;bool Pending=false;FDelegateHandle Handle;
 bool Tick(float){
  auto* W=World.Get();auto* A=Actor.Get();if(!W||!A)return false;
  auto* B=Cast<UVamLegSkeletalMeshComponent>(A->Character->Body);if(!B||!A->Hair->Groom||(GShaderCompilingManager&&GShaderCompilingManager->IsCompiling()))return true;
  if(Pending)return true;
  if(Index>=Views.Num()){
   auto R=VamCaptureHairAudit(A);R->SetArrayField(TEXT("views"),Results);R->SetStringField(TEXT("scope"),TEXT("Actual normal BP; physics disabled; source camera transformed through measured head frame; native UE material/lighting, not pixel-equivalent shading"));FString S;FJsonSerializer::Serialize(R,TJsonWriterFactory<>::Create(&S));FFileHelper::SaveStringToFile(S,*(Directory/TEXT("capture.json")));UGameViewportClient::OnScreenshotCaptured().Remove(Handle);GEngine->Exec(W,TEXT("quit"));return false;
  }
  B->bJiggleEnabled=B->bGluteJiggleEnabled=B->bLegJiggleEnabled=false;
  const auto O=Views[Index]->AsObject();const FTransform Head=B->GetSocketTransform(TEXT("head"));
  const FVector Position=Head.TransformPosition(V(O,TEXT("head_position_cm"))),Forward=Head.TransformVectorNoScale(V(O,TEXT("head_forward"))),Up=Head.TransformVectorNoScale(V(O,TEXT("head_up")));
  Backdrop->SetActorLocation(Head.GetLocation()+Forward*300);Backdrop->SetActorRotation(FRotationMatrix::MakeFromZ(Forward).Rotator());
  Camera->SetActorLocationAndRotation(Position,FRotationMatrix::MakeFromXZ(Forward,Up).Rotator());
  Key->SetWorldRotation(Head.TransformVectorNoScale(V(O,TEXT("head_key_forward"))).Rotation());Fill->SetWorldRotation(Head.TransformVectorNoScale(V(O,TEXT("head_fill_forward"))).Rotation());
  if(++Frame>=120){Pending=true;Frame=0;FScreenshotRequest::RequestScreenshot(false);}return true;
 }
};
FAutoConsoleCommandWithWorldAndArgs Command(TEXT("vam.HairRestCapture"),TEXT("Output BPClass head-relative-camera-json. Actual static Groom comparison."),FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args,UWorld* W){
 if(Args.Num()!=3||!W||!W->IsGameWorld()||!GEngine->GameViewport)return;
 FString S;TSharedPtr<FJsonObject> Input;if(!FFileHelper::LoadFileToString(S,*Args[2])||!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(S),Input))return;
 auto* Class=LoadClass<AVamCharacterActor>(nullptr,*Args[1]);if(!Class)return;
 auto P=MakeShared<FTake>();P->World=W;P->Directory=Args[0];P->Views=Input->GetArrayField(TEXT("views"));IFileManager::Get().MakeDirectory(*P->Directory,true);
 auto* A=W->SpawnActor<AVamCharacterActor>(Class);P->Actor=A;A->Hair->SetHairSimulationEnabled(false);A->BreastContact->SetContactEnabled(false);A->BodyContactResponse->bEnabled=false;A->ActivePose->bBreathing=A->ActivePose->bIdle=A->ActivePose->bBlink=false;A->LoadCharacter();
 auto* Camera=W->SpawnActor<ACameraActor>();P->Camera=Camera;auto* C=Camera->GetCameraComponent();C->FieldOfView=40;C->SetAspectRatio(1.f);C->SetConstraintAspectRatio(false);
 C->PostProcessSettings.bOverride_AutoExposureMethod=true;C->PostProcessSettings.AutoExposureMethod=AEM_Manual;C->PostProcessSettings.bOverride_AutoExposureBias=true;C->PostProcessSettings.AutoExposureBias=0;C->PostProcessSettings.bOverride_AutoExposureApplyPhysicalCameraExposure=true;C->PostProcessSettings.AutoExposureApplyPhysicalCameraExposure=false;C->PostProcessSettings.bOverride_MotionBlurAmount=true;C->PostProcessSettings.MotionBlurAmount=0;
 if(auto* PC=W->GetFirstPlayerController()){PC->SetViewTarget(Camera);if(auto Pawn=PC->GetPawn())Pawn->SetActorHiddenInGame(true);}
 auto* Back=W->SpawnActor<AActor>();P->Backdrop=Back;auto* Mesh=NewObject<UStaticMeshComponent>(Back);Back->SetRootComponent(Mesh);Mesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
 auto* Material=NewObject<UMaterial>(GetTransientPackage());Material->SetShadingModel(MSM_Unlit);auto* Color=NewObject<UMaterialExpressionConstant3Vector>(Material);Color->Constant=FLinearColor(.012f,.016f,.026f);Material->GetExpressionCollection().AddExpression(Color);Material->GetEditorOnlyData()->EmissiveColor.Expression=Color;Material->PostEditChange();Material->ForceRecompileForRendering();Mesh->SetMaterial(0,Material);Mesh->RegisterComponent();Back->SetActorScale3D(FVector(30,30,.01));
 for(int I=0;I<2;++I){auto* L=W->SpawnActor<AActor>();auto* Light=NewObject<UDirectionalLightComponent>(L);L->SetRootComponent(Light);Light->Intensity=I==0?3:.7;Light->RegisterComponent();if(I==0)P->Key=Light;else P->Fill=Light;}
 P->Handle=UGameViewportClient::OnScreenshotCaptured().AddLambda([P](int Width,int Height,const TArray<FColor>& Pixels){if(!P->Pending)return;const auto O=P->Views[P->Index]->AsObject();TArray<uint8> PNG;FImageUtils::CompressImageArray(Width,Height,Pixels,PNG);FFileHelper::SaveArrayToFile(PNG,*(P->Directory/(O->GetStringField(TEXT("name"))+TEXT(".png"))));P->Results.Add(P->Views[P->Index]);++P->Index;P->Pending=false;});
 GEngine->Exec(W,TEXT("r.SetRes 960x960w"));GEngine->Exec(W,TEXT("r.HairStrands.Simulation 0"));GEngine->Exec(W,TEXT("r.ScreenshotDelegate 1"));FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([P](float Dt){return P->Tick(Dt);}));
}));
}
#endif

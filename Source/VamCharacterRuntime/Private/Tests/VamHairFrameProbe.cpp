// Opt-in real game viewport benchmark. No synchronous GPU readback in measured frames.
#include "CoreMinimal.h"
#if WITH_EDITOR && !UE_BUILD_SHIPPING
#include "VamCharacterActor.h"
#include "VamCharacterComponent.h"
#include "VamHairComponent.h"
#include "VamBreastContactComponent.h"
#include "VamBodyContactResponseComponent.h"
#include "VamLegSkeletalMeshComponent.h"
#include "GroomComponent.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "UnrealClient.h"
#include "Camera/CameraActor.h"
#include "GameFramework/PlayerController.h"
#include "Components/DirectionalLightComponent.h"
#include "Containers/Ticker.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "ShaderCompiler.h"
namespace VamHairProbe
{
struct FProbe
{
 TWeakObjectPtr<UWorld> World;
 TArray<TWeakObjectPtr<AVamCharacterActor>> Actors;
 FString Directory,CSV=TEXT("phase,frame,frame_ms,niagara_components\n");
 int32 Phase=0,Frame=0;double Last=0,Start=0;
 void SetPhase(){for(auto P:Actors)if(auto* A=P.Get()){A->Hair->SetHairEnabled(Phase==1);A->Hair->SetHairQuality(EVamHairQuality::Fine);}}
 bool Tick(float)
 {
  auto* W=World.Get();if(!W)return false;const double Now=FPlatformTime::Seconds(),Ms=1000*(Now-Last);Last=Now;
  bool Ready=true;int Niagara=0;
  for(int I=0;I<Actors.Num();++I)if(auto* A=Actors[I].Get())
  {
   auto* B=Cast<UVamLegSkeletalMeshComponent>(A->Character->Body);if(!B){Ready=false;continue;}
   B->bGluteJiggleEnabled=B->bLegJiggleEnabled=true;
   A->SetActorLocation(FVector(5*FMath::Sin((Now-Start)*2),I*65.,2*FMath::Sin((Now-Start)*8)));
   A->SetActorRotation(FRotator(0,15*FMath::Sin((Now-Start)*1.3),0));
   if(Phase==1){if(!A->Hair->Groom)Ready=false;else Niagara+=A->Hair->Groom->NiagaraComponents.Num();}
  }
  if(!Ready||(GShaderCompilingManager&&GShaderCompilingManager->IsCompiling())){Frame=0;return true;}
  const TCHAR* Label=Phase==0?TEXT("off_before"):Phase==1?TEXT("fine"):TEXT("off_after");
  if(Frame==150)FScreenshotRequest::RequestScreenshot(Directory/FString(Label)+TEXT(".png"),false,false);
  if(Frame>=180)CSV+=FString::Printf(TEXT("%s,%d,%.6f,%d\n"),Label,Frame-180,Ms,Niagara);
  if(++Frame<480)return true;
  UE_LOG(LogTemp,Display,TEXT("VAM_HAIR_BENCHMARK finished %s"),Label);Frame=0;
  if(++Phase==3){FFileHelper::SaveStringToFile(CSV,*(Directory/TEXT("frames.csv")));GEngine->Exec(W,TEXT("quit"));return false;}
  SetPhase();return true;
 }
};
FAutoConsoleCommandWithWorldAndArgs Command(TEXT("vam.HairBenchmark"),TEXT("Actual viewport: output directory, BP class path, optional character count."),FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args,UWorld* W)
{
 if(Args.Num()<2||!W||!W->IsGameWorld()||!GEngine->GameViewport){UE_LOG(LogTemp,Error,TEXT("VAM_HAIR_BENCHMARK requires game viewport, directory and BP"));return;}
 auto* Class=LoadClass<AVamCharacterActor>(nullptr,*Args[1]);if(!Class){GEngine->Exec(W,TEXT("quit"));return;}
 auto P=MakeShared<FProbe>();P->World=W;P->Directory=Args[0];IFileManager::Get().MakeDirectory(*P->Directory,true);const int N=Args.Num()>2?FMath::Clamp(FCString::Atoi(*Args[2]),1,4):1;
 for(int I=0;I<N;++I){auto* A=W->SpawnActor<AVamCharacterActor>(Class,FVector(0,I*65.,0),FRotator::ZeroRotator);A->BreastContact->SetContactEnabled(false);A->BodyContactResponse->bEnabled=false;A->LoadCharacter();P->Actors.Add(A);}
 P->SetPhase();auto* Camera=W->SpawnActor<ACameraActor>(FVector(-180,(N-1)*32.5+60,172),FRotator(-5,-18,0));if(auto* PC=W->GetFirstPlayerController())PC->SetViewTarget(Camera);
 for(FRotator R:{FRotator(-25,0,0),FRotator(-35,160,0)}){auto* A=W->SpawnActor<AActor>();auto* L=NewObject<UDirectionalLightComponent>(A);A->SetRootComponent(L);L->Intensity=3;L->RegisterComponent();L->SetWorldRotation(R);}
 GEngine->Exec(W,TEXT("r.VSync 0"));GEngine->Exec(W,TEXT("t.MaxFPS 0"));GEngine->Exec(W,TEXT("r.SetRes 1920x1080w"));
 FFileHelper::SaveStringToFile(FString::Printf(TEXT("BP=%s\ncharacters=%d\nviewport=1920x1080\nWarmup=180, measured=300 per phase. Root sway and yaw; contact off, glute/leg Jiggle on.\n"),*Args[1],N),*(P->Directory/TEXT("protocol.txt")));
 P->Last=P->Start=FPlatformTime::Seconds();FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([P](float Dt){return P->Tick(Dt);}));
}));
}
#endif

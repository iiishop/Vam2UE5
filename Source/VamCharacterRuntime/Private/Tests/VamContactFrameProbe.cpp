// Opt-in development benchmark. Does not modify or save project assets.
#if !UE_BUILD_SHIPPING
#include "CoreMinimal.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "HAL/PlatformMisc.h"
#include "Containers/Ticker.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "UnrealClient.h"
#include "Camera/CameraActor.h"
#include "GameFramework/PlayerController.h"
#include "Components/StaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Engine/StaticMesh.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "VamCharacterActor.h"
#include "VamCharacterComponent.h"
#include "VamRuntimeConfiguration.h"
#include "VamBreastContactComponent.h"
#include "VamBreastSkeletalMeshComponent.h"
namespace
{
struct FContactFrameProbe : TSharedFromThis<FContactFrameProbe>
{
 TWeakObjectPtr<UWorld> World;
 TArray<TWeakObjectPtr<AVamCharacterActor>> Actors;
 TArray<TWeakObjectPtr<UStaticMeshComponent>> Probes;
 bool Motion=false;double PhaseStart=0;TArray<FVector> HeldPositions,Directions;
 FString CSV=TEXT("phase,frame,frame_ms,active_solvers,max_contact_cm\n");
 int Phase=0,Frame=0;double Last=0;FString Directory;
 bool Tick(float)
 {
  auto* W=World.Get();if(!W)return false;
  const double Now=FPlatformTime::Seconds();const double Ms=(Now-Last)*1000;Last=Now;
  const int Warmup=Phase==0?180:120;
  if(Motion&&Phase==3)for(int I=0;I<Probes.Num();++I)Probes[I]->SetWorldLocation(HeldPositions[I]+Directions[I]*FMath::Sin((Now-PhaseStart)*PI));
  if(Frame==Warmup-40)
  {
   IFileManager::Get().MakeDirectory(*Directory,true);
   FScreenshotRequest::RequestScreenshot(Directory/FString::Printf(TEXT("phase-%d.png"),Phase),false,false);
   const auto Size=GEngine->GameViewport->Viewport->GetSizeXY();
   UE_LOG(LogTemp,Display,TEXT("CONTACT_FRAME_PROBE viewport %dx%d"),Size.X,Size.Y);
  }
  if(Frame>=Warmup)
  {
   int Solvers=0;double Residual=0;
   for(auto A:Actors)if(A.IsValid()){Solvers+=A->BreastContact->GetActiveSolverCount();Residual=FMath::Max(Residual,A->BreastContact->GetMaxContactResidualCm());}
   const TCHAR* Label=Phase==0?TEXT("off_before"):Phase==1?TEXT("on_idle"):Phase==2?TEXT("on_pressed"):Motion&&Phase==3?TEXT("on_moving"):Motion&&Phase==4?TEXT("released"):TEXT("off_after");
   CSV+=FString::Printf(TEXT("%s,%d,%.6f,%d,%.6f\n"),Label,Frame-Warmup,Ms,Solvers,Residual);
  }
  if(++Frame<Warmup+300)return true;
  UE_LOG(LogTemp,Display,TEXT("CONTACT_FRAME_PROBE phase %d finished"),Phase);
  ++Phase;Frame=0;PhaseStart=Now;
  if(Phase==(Motion?6:4))
  {
   IFileManager::Get().MakeDirectory(*Directory,true);
   FFileHelper::SaveStringToFile(CSV,*(Directory/TEXT("frames.csv")));
   FString Info;
   for(auto A:Actors)if(A.IsValid())Info+=A->GetClass()->GetPathName()+TEXT("\n")+A->BreastContact->Diagnostics()+TEXT("\n");
   FFileHelper::SaveStringToFile(Info,*(Directory/TEXT("instances.txt")));
   GEngine->Exec(W,TEXT("quit"));return false;
  }
  for(int I=0;I<Actors.Num();++I)if(auto* A=Actors[I].Get())
  {
   A->SetBreastContactEnabled(Phase!=(Motion?5:3));
   if(Phase==2)
   {
    auto* B=Cast<UVamBreastSkeletalMeshComponent>(A->Character->Body);
    if(!B||B->RestSides.Num()!=2){UE_LOG(LogTemp,Error,TEXT("CONTACT_FRAME_PROBE missing breast rest"));GEngine->Exec(W,TEXT("quit"));return false;}
    const auto& Rest=B->RestSides[0];const auto Local=B->GetComponentSpaceTransforms()[Rest.AnchorBone];
    auto* Profile=A->Character->RuntimeConfiguration->BreastContact.LoadSynchronous();
    const double Radius=Rest.EffectiveRadiusCm*.55;FVector Center=Rest.COM;
    TArray<FVector> Points;for(const auto& P:Profile->Particles)Points.Add(P.Rest);
    const double Front=Profile->ProbeFront(Points,Local,Center,Radius,false,true,0);
    Center.X=Front+Radius-.2*Rest.EffectiveDepthCm;
    UE_LOG(LogTemp,Display,TEXT("CONTACT_FRAME_PROBE instance %d radius %.4f front %.4f depth %.4f COM %s body %s anchor %s"),I,Radius,Front,Rest.EffectiveDepthCm,*Rest.COM.ToString(),*B->GetComponentTransform().ToString(),*Local.ToString());
    Probes[I]->SetWorldScale3D(FVector(Radius/50.));
    Probes[I]->SetWorldLocation((Local*B->GetComponentTransform()).TransformPosition(Center));
    Probes[I]->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);Probes[I]->SetVisibility(true);
    HeldPositions.Add(Probes[I]->GetComponentLocation());Directions.Add((Local*B->GetComponentTransform()).TransformVectorNoScale(FVector(Rest.EffectiveDepthCm*.05,0,0)));
   }
   if(Phase==(Motion?4:3)){Probes[I]->SetVisibility(false);Probes[I]->SetCollisionEnabled(ECollisionEnabled::NoCollision);}
  }
  return true;
 }
};
FAutoConsoleCommandWithWorldAndArgs ContactFrameCommand(TEXT("vam.ContactBenchmark"),TEXT("Opt-in two-character real game viewport frame benchmark; optional output directory."),
 FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args,UWorld* W)
 {
  if(!W||!W->IsGameWorld()||!GEngine->GameViewport){UE_LOG(LogTemp,Error,TEXT("CONTACT_FRAME_PROBE requires -game viewport"));return;}
  auto Probe=MakeShared<FContactFrameProbe>();Probe->World=W;Probe->Motion=Args.Num()>1&&Args[1]==TEXT("motion");
  Probe->Directory=Args.Num()?Args[0]:FPaths::ProjectSavedDir()/TEXT("ContactTwoCharacters");
  auto* Config=LoadObject<UVamRuntimeConfiguration>(nullptr,TEXT("/Game/VamRuntime/R_619448199d803edf1ab83af9/RC_Runtime.RC_Runtime"));
  UClass* Class=LoadClass<AVamCharacterActor>(nullptr,TEXT("/Game/VamRuntime/R_619448199d803edf1ab83af9/BP_VamCharacter.BP_VamCharacter_C"));
  if(!Config||!Class){UE_LOG(LogTemp,Error,TEXT("CONTACT_FRAME_PROBE missing assets"));GEngine->Exec(W,TEXT("quit"));return;}
  // Read-only actual profile export for the separate matrix preflight.
  {
   const auto* P=Config->BreastContact.LoadSynchronous();FString Data=TEXT("{\"vertices\":[");
   for(int I=0;I<P->Particles.Num();++I){const auto V=P->Particles[I].Rest;Data+=FString::Printf(TEXT("%s[%.17g,%.17g,%.17g]"),I?TEXT(","):TEXT(""),V.X,V.Y,V.Z);}
   Data+=TEXT("],\"fixed\":[");for(int I=0;I<P->Particles.Num();++I)Data+=FString::Printf(TEXT("%s%s"),I?TEXT(","):TEXT(""),P->Particles[I].bKinematic?TEXT("true"):TEXT("false"));
   Data+=TEXT("],\"tets\":[");for(int I=0;I<P->Tetrahedra.Num();++I){const auto T=P->Tetrahedra[I];Data+=FString::Printf(TEXT("%s[%d,%d,%d,%d]"),I?TEXT(","):TEXT(""),T[0],T[1],T[2],T[3]);}
   Data+=TEXT("],\"surface\":[");for(int I=0;I<P->BoundaryTriangles.Num();++I){const auto T=P->BoundaryTriangles[I];Data+=FString::Printf(TEXT("%s[%d,%d,%d]"),I?TEXT(","):TEXT(""),T[0],T[1],T[2]);}
   Data+=FString::Printf(TEXT("],\"young_pa\":%.17g,\"nu\":%.17g}"),double(P->YoungModulusPa),double(P->PoissonRatio));
   IFileManager::Get().MakeDirectory(*Probe->Directory,true);FFileHelper::SaveStringToFile(Data,*(Probe->Directory/TEXT("mesh.json")));
  }
  for(int I=0;I<2;++I)
  {
   auto* A=W->SpawnActorDeferred<AVamCharacterActor>(Class,FTransform(FVector(0,I==0?-75:75,0)));
   A->Character->RuntimeConfiguration=Config;A->BreastContact->bEnabled=false;A->FinishSpawning(FTransform(FVector(0,I==0?-75:75,0)));Probe->Actors.Add(A);
   auto* PActor=W->SpawnActor<AActor>();auto* P=NewObject<UStaticMeshComponent>(PActor);PActor->SetRootComponent(P);
   P->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Sphere.Sphere")));P->SetMobility(EComponentMobility::Movable);
   P->SetCollisionObjectType(ECC_WorldDynamic);P->SetCollisionResponseToAllChannels(ECR_Block);P->SetCollisionEnabled(ECollisionEnabled::NoCollision);
   P->SetVisibility(false);P->RegisterComponent();Probe->Probes.Add(P);
  }
  auto* Camera=W->SpawnActor<ACameraActor>(FVector(420,0,100),FRotator(0,180,0));
  if(auto* PC=W->GetFirstPlayerController())PC->SetViewTarget(Camera);
  for(const FVector Position:{FVector(200,-180,250),FVector(100,180,210)})
  {
   auto* LightActor=W->SpawnActor<AActor>();auto* Light=NewObject<UPointLightComponent>(LightActor);LightActor->SetRootComponent(Light);
   Light->SetIntensity(18000);Light->SetAttenuationRadius(1500);Light->SetMobility(EComponentMobility::Movable);Light->RegisterComponent();Light->SetWorldLocation(Position);
  }
  GEngine->Exec(W,TEXT("r.SetRes 1920x1080w"));GEngine->Exec(W,TEXT("r.VSync 0"));GEngine->Exec(W,TEXT("t.MaxFPS 0"));
  Probe->Last=FPlatformTime::Seconds();FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Probe](float Dt){return Probe->Tick(Dt);}));
  UE_LOG(LogTemp,Display,TEXT("CONTACT_FRAME_PROBE started two BP instances with normal game tick and viewport"));
 }));
}
#endif

#include "CoreMinimal.h"
#if WITH_EDITOR
#include "Editor.h"
#include "PlayInEditorDataTypes.h"
#include "Tests/AutomationEditorCommon.h"
#include "HAL/IConsoleManager.h"
#include "Engine/Engine.h"
// Explicit isolated capture only; never invoked during ordinary editing.
static FAutoConsoleCommand VamHairPIECommand(TEXT("vam.HairPIECapture"),TEXT("Start isolated real PIE and dispatch the ordinary viewport hair recorder."),FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
{
 if(!GEditor||GEditor->PlayWorld||Args.Num()<4)return;
 FAutomationEditorCommonUtils::CreateNewMap();FRequestPlaySessionParams Params;Params.bAllowOnlineSubsystem=false;GEditor->RequestPlaySession(Params);GEditor->StartQueuedPlaySessionRequest();
 if(!GEditor->PlayWorld)return;FString Command=TEXT("vam.HairCapture");for(const FString& Arg:Args)Command+=TEXT(" \"")+Arg+TEXT("\"");GEngine->Exec(GEditor->PlayWorld,*Command);
}));
static FAutoConsoleCommand VamHairRestPIECommand(TEXT("vam.HairRestPIECapture"),TEXT("Start isolated real PIE and capture static hair against the source cameras."),FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
{
 if(!GEditor||GEditor->PlayWorld||Args.Num()!=3)return;
 FAutomationEditorCommonUtils::CreateNewMap();FRequestPlaySessionParams Params;Params.bAllowOnlineSubsystem=false;GEditor->RequestPlaySession(Params);GEditor->StartQueuedPlaySessionRequest();
 if(!GEditor->PlayWorld)return;FString Command=TEXT("vam.HairRestCapture");for(const FString& Arg:Args)Command+=TEXT(" \"")+Arg+TEXT("\"");GEngine->Exec(GEditor->PlayWorld,*Command);
}));
#endif

#include "CoreMinimal.h"
#include "Editor.h"
#include "EditorViewportClient.h"
#include "LevelEditor.h"
#include "Editor/EditorPerformanceSettings.h"
#include "NiagaraScript.h"
#include "NiagaraSystem.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraScriptSource.h"
#include "NiagaraGraph.h"
#include "NiagaraNodeFunctionCall.h"
#include "Misc/FileHelper.h"
#include "HAL/IConsoleManager.h"
#include "VamCharacterActor.h"
#include "VamCharacterComponent.h"
#include "VamLegSkeletalMeshComponent.h"
#include "Tests/VamHairCaptureAudit.h"
#include "ShaderCompiler.h"
#include "Serialization/JsonSerializer.h"

static FAutoConsoleCommand HairEditorGpuAudit(TEXT("vam.HairEditorGpuAudit"),TEXT("Read existing hair GPU diagnostics in the actual Editor world after shader compilation"),FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
{
 if(Args.Num()!=2||(GShaderCompilingManager&&GShaderCompilingManager->IsCompiling()))return;
 auto* Actor=FindObject<AVamCharacterActor>(nullptr,*Args[1]);
 if(!Actor||!Actor->GetWorld()||Actor->GetWorld()->WorldType!=EWorldType::Editor)return;
 auto Result=VamCaptureHairAudit(Actor);Result->SetStringField(TEXT("world"),TEXT("Editor"));Result->SetStringField(TEXT("actor"),Actor->GetPathName());
 FString Text;FJsonSerializer::Serialize(Result.ToSharedRef(),TJsonWriterFactory<>::Create(&Text));FFileHelper::SaveStringToFile(Text,*Args[0]);
}));
static FAutoConsoleCommand HairCompiledAudit(TEXT("vam.HairCompiledAudit"),TEXT("Dump the compiled GPU code of the specified hair system without changing it"),FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
{
 if(Args.Num()!=2)return;
 auto* System=LoadObject<UNiagaraSystem>(nullptr,*Args[1]);if(!System){UE_LOG(LogTemp,Error,TEXT("Hair compiled audit: system missing"));return;}
 System->WaitForCompilationComplete(true,false);FString Out;
 for(const auto& Handle:System->GetEmitterHandles())if(auto* Data=Handle.GetEmitterData())if(auto* Script=Data->GetGPUComputeScript())
 {
  Out+=TEXT("// ")+Script->GetPathName()+TEXT("\n")+Script->GetVMExecutableData().LastHlslTranslationGPU+TEXT("\n");
 }
 if(Out.Len()<1000){UE_LOG(LogTemp,Error,TEXT("Hair compiled audit: compiled GPU code unavailable"));return;}
 FFileHelper::SaveStringToFile(Out,*Args[0]);
}));
static FAutoConsoleCommand HairGraphAudit(TEXT("vam.HairGraphAudit"),TEXT("Inspect native hair modules without changing them"),FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
{
 if(Args.IsEmpty())return;FString Out;
 for(const TCHAR* Name:{TEXT("ProjectGroomState"),TEXT("ReportSharedPosition"),TEXT("InterpolateGroomRoots"),TEXT("AdvanceGroomState"),TEXT("AdvectGroomState"),TEXT("SolveGroomState"),TEXT("InitGroomState")})
 {
  auto* Script=LoadObject<UNiagaraScript>(nullptr,*(FString(TEXT("/HairStrands/Emitters/"))+Name+TEXT(".")+Name));if(!Script)continue;
  auto* Source=Cast<UNiagaraScriptSource>(Script->GetLatestSource());if(!Source||!Source->NodeGraph)continue;Out+=Script->GetPathName()+TEXT("\n");
  for(const auto& Node:Source->NodeGraph->Nodes){Out+=Node->GetName()+TEXT(" ")+Node->GetClass()->GetName()+TEXT(" ")+Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString()+TEXT("\n");if(auto* Call=Cast<UNiagaraNodeFunctionCall>(Node))if(Call->FunctionScript)Out+=TEXT("script: ")+Call->FunctionScript->GetPathName()+TEXT("\n");if(auto* P=FindFProperty<FStrProperty>(Node->GetClass(),TEXT("CustomHlsl")))Out+=P->GetPropertyValue_InContainer(Node)+TEXT("\n");for(auto* Pin:Node->Pins){Out+=FString::Printf(TEXT(" %s %s = %s links:"),Pin->Direction==EGPD_Input?TEXT("in"):TEXT("out"),*Pin->PinName.ToString(),*Pin->DefaultValue);for(auto* Link:Pin->LinkedTo)Out+=TEXT(" ")+Link->GetOwningNode()->GetName()+TEXT(".")+Link->PinName.ToString();Out+=TEXT("\n");}}
 }
 FFileHelper::SaveStringToFile(Out,*Args[0]);
}));

// Opt-in verification helper: use the real editor world tick, never manually tick hair.
static FAutoConsoleCommand HairEditorRealtime(TEXT("vam.HairEditorRealtime"),TEXT("Enable realtime in this isolated editor verification session"),FConsoleCommandDelegate::CreateLambda([](){
 GetMutableDefault<UEditorPerformanceSettings>()->bThrottleCPUWhenNotForeground=false;
 auto& Level=FModuleManager::LoadModuleChecked<FLevelEditorModule>(TEXT("LevelEditor"));
 if(auto Tabs=Level.GetLevelEditorTabManager())Tabs->TryInvokeTab(FName(TEXT("LevelEditorViewport")));
 if(GEditor)for(auto* View:GEditor->GetAllViewportClients())if(View){View->SetRealtime(true);View->Invalidate();}
}));

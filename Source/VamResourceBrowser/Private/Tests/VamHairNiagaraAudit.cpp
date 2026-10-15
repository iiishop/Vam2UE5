#include "CoreMinimal.h"
#include "NiagaraScript.h"
#include "NiagaraScriptSource.h"
#include "NiagaraGraph.h"
#include "NiagaraNodeFunctionCall.h"
#include "Misc/FileHelper.h"
#include "HAL/IConsoleManager.h"
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

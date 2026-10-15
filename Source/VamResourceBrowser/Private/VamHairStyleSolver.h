#pragma once
#include "NiagaraSystem.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraScript.h"
#include "NiagaraScriptSource.h"
#include "NiagaraGraph.h"
#include "NiagaraNodeCustomHlsl.h"
#include "NiagaraNodeWithDynamicPins.h"

// Private copies keep the engine's solver assets untouched. The style constraint
// participates in the solver state, before velocity reconstruction and writeback.
static UNiagaraSystem* BuildHairStyleSolver(const FString& Root, const FString& Kind, FString& Error)
{
    const FString SystemPath=Root/TEXT("NS_Style_")+Kind;
    if(auto* Existing=FindObject<UNiagaraSystem>(nullptr,*(SystemPath+TEXT(".")+FPackageName::GetLongPackageAssetName(SystemPath))))return Existing;
    auto* Native=LoadObject<UNiagaraSystem>(nullptr,TEXT("/HairStrands/Emitters/StableRodsSystem.StableRodsSystem"));
    auto* Project=LoadObject<UNiagaraScript>(nullptr,TEXT("/HairStrands/Emitters/ProjectGroomState.ProjectGroomState"));
    if(!Native||!Project){Error=TEXT("Native Groom solver missing");return nullptr;}
    const FString ScriptPath=Root/TEXT("ProjectStyle_")+Kind;
    auto* Copy=DuplicateObject<UNiagaraScript>(Project,CreatePackage(*ScriptPath),*FPackageName::GetLongPackageAssetName(ScriptPath));
    Copy->SetFlags(RF_Public|RF_Standalone);
    auto* Source=Cast<UNiagaraScriptSource>(Copy->GetLatestSource());auto* Graph=Source?Source->NodeGraph.Get():nullptr;
    if(!Graph){Error=TEXT("Native Groom project graph missing");return nullptr;}
    UNiagaraNodeWithDynamicPins* Get=nullptr;UEdGraphPin* Destination=nullptr;UEdGraphPin* Position=nullptr;
    for(UEdGraphNode* N:Graph->Nodes)
    {
        if(auto* Call=Cast<UNiagaraNodeFunctionCall>(N);Call&&Call->FunctionScript==LoadObject<UNiagaraScript>(nullptr,TEXT("/HairStrands/Emitters/ReportSharedPosition.ReportSharedPosition")))
        {
            Position=Call->FindPin(TEXT("NodePosition"),EGPD_Output);
            if(Position&&Position->LinkedTo.Num()==1)Destination=Position->LinkedTo[0];
            auto* In=Call->FindPin(TEXT("NodePosition"),EGPD_Input);
            if(In&&In->LinkedTo.Num()==1)Get=Cast<UNiagaraNodeWithDynamicPins>(In->LinkedTo[0]->GetOwningNode());
        }
    }
    if(!Get||!Destination||!Position){Error=TEXT("Native Groom graph contract changed");return nullptr;}
    auto* H=NewObject<UNiagaraNodeCustomHlsl>(Graph);Graph->AddNode(H,false,false);H->CreateNewGuid();
    H->Signature.Inputs={FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(),TEXT("Current")),FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(),TEXT("Rest")),FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(),TEXT("Dt"))};
    H->Signature.Outputs={FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(),TEXT("Result"))};
    H->AllocateDefaultPins();
    Position->BreakLinkTo(Destination);Position->MakeLinkTo(H->FindPin(TEXT("Current"),EGPD_Input));H->FindPin(TEXT("Result"),EGPD_Output)->MakeLinkTo(Destination);
    auto AddGet=[&](FName Name,FName Input){
        auto* InputPin=H->FindPin(Input,EGPD_Input);
        auto* Pin=Get->CreatePin(EGPD_Output,InputPin->PinType,Name);Pin->PersistentGuid=FGuid::NewGuid();Pin->MakeLinkTo(InputPin);
    };
    AddGet(TEXT("Transient.TargetPosition"),TEXT("Rest"));AddGet(TEXT("Emitter.StepTime"),TEXT("Dt"));
    const float Frequency=Kind==TEXT("compact")?35.f:Kind==TEXT("supported")?45.f:3.f;
    const float Limit=Kind==TEXT("compact")?.25f:Kind==TEXT("supported")?.15f:10.f;
    FindFProperty<FStrProperty>(H->GetClass(),TEXT("CustomHlsl"))->SetPropertyValue_InContainer(H,FString::Printf(TEXT("Result = Current;\n#if GPU_SIMULATION\nfloat3 D = Current - Rest;\nfloat W = 6.283185307 * %.9f;\nfloat A = Dt*Dt*W*W;\nD /= 1.0+A;\nD *= min(1.0, %.9f / max(length(D), 1.e-6));\nResult = Rest+D;\nSharedNodePosition[GGroupThreadId.x]=Result;\nGroupMemoryBarrier();\n#endif\n"),Frequency,Limit));
    static_cast<UNiagaraScriptSourceBase*>(Source)->MarkNotSynchronized(TEXT("Style constraint"));
    if(!VamHairBuild::Save(Copy)){Error=TEXT("Style module save failed");return nullptr;}
    auto* System=DuplicateObject<UNiagaraSystem>(Native,CreatePackage(*SystemPath),*FPackageName::GetLongPackageAssetName(SystemPath));System->SetFlags(RF_Public|RF_Standalone);
    int32 Replaced=0;
    TMap<UNiagaraScript*,UNiagaraScript*> Redirect;Redirect.Add(Project,Copy);
    TFunction<UNiagaraScript*(UNiagaraScript*)> Rewrite=[&](UNiagaraScript* Original)->UNiagaraScript*
    {
        if(!Original)return nullptr;if(auto** Known=Redirect.Find(Original))return *Known;
        Redirect.Add(Original,Original);
        auto* OS=Cast<UNiagaraScriptSource>(Original->GetLatestSource());if(!OS||!OS->NodeGraph)return Original;
        TMap<UNiagaraScript*,UNiagaraScript*> Changes;
        for(UEdGraphNode* N:OS->NodeGraph->Nodes)if(auto* Call=Cast<UNiagaraNodeFunctionCall>(N);Call&&Call->FunctionScript){auto* New=Rewrite(Call->FunctionScript);if(New!=Call->FunctionScript)Changes.Add(Call->FunctionScript,New);}
        if(Changes.IsEmpty())return Original;
        const FString P=Root/FString::Printf(TEXT("Style_%s_%08x_%s"),*Kind,GetTypeHash(Original->GetPathName()),*Original->GetName());
        auto* New=DuplicateObject<UNiagaraScript>(Original,CreatePackage(*P),*FPackageName::GetLongPackageAssetName(P));New->SetFlags(RF_Public|RF_Standalone);
        auto* NS=Cast<UNiagaraScriptSource>(New->GetLatestSource());
        for(UEdGraphNode* N:NS->NodeGraph->Nodes)if(auto* Call=Cast<UNiagaraNodeFunctionCall>(N);Call&&Changes.Contains(Call->FunctionScript)){Call->FunctionScript=Changes[Call->FunctionScript];Call->RefreshFromExternalChanges();}
        static_cast<UNiagaraScriptSourceBase*>(NS)->MarkNotSynchronized(TEXT("Style dependency"));VamHairBuild::Save(New);Redirect[Original]=New;return New;
    };
    for(auto& Handle:System->GetEmitterHandles())
    {
        auto* Data=Handle.GetEmitterData();auto* ES=Data?Cast<UNiagaraScriptSource>(Data->GraphSource):nullptr;
        if(!ES||!ES->NodeGraph)continue;
        for(UEdGraphNode* N:ES->NodeGraph->Nodes)if(auto* Call=Cast<UNiagaraNodeFunctionCall>(N);Call&&Call->FunctionScript){auto* New=Rewrite(Call->FunctionScript);if(New!=Call->FunctionScript){Call->FunctionScript=New;Call->RefreshFromExternalChanges();++Replaced;}}
        static_cast<UNiagaraScriptSourceBase*>(ES)->MarkNotSynchronized(TEXT("Style constraint"));
    }
    if(!Replaced){Error=TEXT("Style solver did not replace a project stage");return nullptr;}
    System->RequestCompile(true);System->WaitForCompilationComplete(true,false);
    if(!VamHairBuild::Save(System)){Error=TEXT("Style solver save failed");return nullptr;}return System;
}




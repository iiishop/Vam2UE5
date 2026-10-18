#pragma once
#include "NiagaraSystem.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraScript.h"
#include "NiagaraScriptSource.h"
#include "NiagaraGraph.h"
#include "NiagaraNodeCustomHlsl.h"
#include "NiagaraNodeWithDynamicPins.h"
#include "Niagara/NiagaraDataInterfaceHairStrands.h"
#include "VamHairSourceConstraints.h"

// Tied/support curves may contain an intentionally buried attachment path.
// A negative rest clearance is not a moving contact: repeatedly ejecting that
// path fights its attachment and sends impulses down the exposed strand.
static bool ConfigureHairContact(UNiagaraScript* Script,bool Supported,float AnchorU=-1.f)
{
    auto* Source=Cast<UNiagaraScriptSource>(Script->GetLatestSource());
    if(!Source||!Source->NodeGraph)return false;
    auto* Graph=Source->NodeGraph.Get();const auto Nodes=Graph->Nodes;int32 Count=0;
    for(UEdGraphNode* Node:Nodes)
    {
        auto* Call=Cast<UNiagaraNodeFunctionCall>(Node);
        if(!Call||!Call->Signature.Name.ToString().Contains(TEXT("CollisionConstraint")))continue;
        if(!Supported)
        {
            // Visible mobile strands may not inherit a negative clearance from
            // an intersecting import. Native root nodes remain pinned/exempt.
            auto* Depth=Call->FindPin(TEXT("Penetration Depth"),EGPD_Input);
            if(!Depth||Depth->LinkedTo.Num()!=1)return false;
            auto* Previous=Depth->LinkedTo[0];auto* Clamp=NewObject<UNiagaraNodeCustomHlsl>(Graph);Graph->AddNode(Clamp,false,false);Clamp->CreateNewGuid();
            Clamp->Signature.Inputs={FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(),TEXT("Clearance")),FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(),TEXT("Radius"))};Clamp->Signature.Outputs={FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(),TEXT("Result"))};Clamp->AllocateDefaultPins();
            Depth->BreakLinkTo(Previous);Previous->MakeLinkTo(Clamp->FindPin(TEXT("Clearance"),EGPD_Input));Clamp->FindPin(TEXT("Result"),EGPD_Output)->MakeLinkTo(Depth);
            auto* Parameters=Cast<UNiagaraNodeWithDynamicPins>(Previous->GetOwningNode());if(!Parameters)return false;
            auto* Radius=Parameters->FindPin(TEXT("Emitter.CollisionRadius"),EGPD_Output);auto* RadiusInput=Clamp->FindPin(TEXT("Radius"),EGPD_Input);
            if(!Radius){Radius=Parameters->CreatePin(EGPD_Output,RadiusInput->PinType,TEXT("Emitter.CollisionRadius"));Radius->PersistentGuid=FGuid::NewGuid();}Radius->MakeLinkTo(RadiusInput);
            FindFProperty<FStrProperty>(Clamp->GetClass(),TEXT("CustomHlsl"))->SetPropertyValue_InContainer(Clamp,TEXT("Result=max(Radius,Clearance);"));++Count;if(AnchorU<0)continue;
        }
        auto* Enable=Call->FindPin(TEXT("Enable Constraint"),EGPD_Input);
        if(!Enable||Enable->LinkedTo.Num()!=1)return false;
        auto* Previous=Enable->LinkedTo[0];auto* Get=Cast<UNiagaraNodeWithDynamicPins>(Previous->GetOwningNode());
        if(!Get)return false;
        auto* Gate=NewObject<UNiagaraNodeCustomHlsl>(Graph);Graph->AddNode(Gate,false,false);Gate->CreateNewGuid();
        Gate->Signature.Inputs={FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(),TEXT("Enabled")),FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(),TEXT("RestDistance"))};
        Gate->Signature.Outputs={FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(),TEXT("Result"))};Gate->AllocateDefaultPins();
        Enable->BreakLinkTo(Previous);Previous->MakeLinkTo(Gate->FindPin(TEXT("Enabled"),EGPD_Input));Gate->FindPin(TEXT("Result"),EGPD_Output)->MakeLinkTo(Enable);
        auto* In=Gate->FindPin(TEXT("RestDistance"),EGPD_Input);auto* Rest=Get->FindPin(TEXT("Particles.RestDistance"),EGPD_Output);
        if(!Rest){Rest=Get->CreatePin(EGPD_Output,In->PinType,TEXT("Particles.RestDistance"));Rest->PersistentGuid=FGuid::NewGuid();}
        Rest->MakeLinkTo(In);
        FindFProperty<FStrProperty>(Gate->GetClass(),TEXT("CustomHlsl"))->SetPropertyValue_InContainer(Gate,Supported?TEXT("Result=Enabled && RestDistance>=0.0;"):FString::Printf(TEXT("Result=Enabled;\n#if GPU_SIMULATION\nResult=Enabled && (RestDistance>=0.0 || max(0.0,float(GGroupThreadId.x %% 32)-1.0)/30.0>%.9f);\n#endif\n"),AnchorU));++Count;
    }
    static_cast<UNiagaraScriptSourceBase*>(Source)->MarkNotSynchronized(TEXT("Buried support contact classification"));
    return Count>0;
}

// Private copies keep the engine's solver assets untouched. The style constraint
// participates after rod projection. Loose strands retain final collision priority;
// tied bundles also enforce their bounded attachment volume at final output.
static UNiagaraSystem* BuildHairStyleSolver(const FString& Root, const FString& Kind, int32 NodeCount, int32 Iterations, int32 GroupIndex, const TSharedPtr<FJsonObject>& SourceConstraints, FString& Error)
{
    float AnchorU=-1.f;
    if(SourceConstraints&&SourceConstraints->HasTypedField<EJson::Object>(TEXT("automatic_bundle_anchor")))AnchorU=SourceConstraints->GetObjectField(TEXT("automatic_bundle_anchor"))->GetNumberField(TEXT("curve_coordinate"));
    const FString Key=Kind+FString::Printf(TEXT("_%d_G%d"),NodeCount,GroupIndex);
    const FString SystemPath=Root/TEXT("NS_Style_")+Key;
    if(auto* Existing=FindObject<UNiagaraSystem>(nullptr,*(SystemPath+TEXT(".")+FPackageName::GetLongPackageAssetName(SystemPath))))return Existing;
    auto* Native=LoadObject<UNiagaraSystem>(nullptr,TEXT("/HairStrands/Emitters/StableRodsSystem.StableRodsSystem"));
    auto* Project=LoadObject<UNiagaraScript>(nullptr,TEXT("/HairStrands/Emitters/ProjectGroomState.ProjectGroomState"));
    if(!Native||!Project){Error=TEXT("Native Groom solver missing");return nullptr;}
    const FString ScriptPath=Root/TEXT("ProjectStyle_")+Key;
    auto* Copy=DuplicateObject<UNiagaraScript>(Project,CreatePackage(*ScriptPath),*FPackageName::GetLongPackageAssetName(ScriptPath));
    Copy->SetFlags(RF_Public|RF_Standalone);
    auto* Source=Cast<UNiagaraScriptSource>(Copy->GetLatestSource());auto* Graph=Source?Source->NodeGraph.Get():nullptr;
    if(!Graph){Error=TEXT("Native Groom project graph missing");return nullptr;}
    int32 RefreshedContacts=0;UNiagaraNodeFunctionCall* ContactQuery=nullptr;
    for(UEdGraphNode* N:Graph->Nodes)
    {
        if(auto* Call=Cast<UNiagaraNodeFunctionCall>(N);Call&&Call->Signature.Name==FName(TEXT("GetElementPoint")))
        {
            // A multi-sphere head can change its nearest primitive during rod
            // projection. Query the current position instead of the cached index.
            Call->Signature.Name=TEXT("GetClosestPoint");
            Call->Signature.Inputs.RemoveAll([](const FNiagaraVariable& V){return V.GetName()==FName(TEXT("Element Index"));});
            Call->RefreshFromExternalChanges();ContactQuery=Call;++RefreshedContacts;
        }
    }
    if(RefreshedContacts!=1){Error=TEXT("Native Groom closest-contact graph contract changed");return nullptr;}
    // Contact-query revision 2. Rod projection updates shared memory. Query that same current position,
    // rather than a parameter-map position from before the projection.
    auto* ContactInput=ContactQuery->FindPin(TEXT("Node Position"),EGPD_Input);
    if(!ContactInput||ContactInput->LinkedTo.Num()!=1){Error=TEXT("Contact position contract changed");return nullptr;}
    auto* ContactSource=ContactInput->LinkedTo[0];
    auto* CurrentNode=NewObject<UNiagaraNodeCustomHlsl>(Graph);Graph->AddNode(CurrentNode,false,false);CurrentNode->CreateNewGuid();
    CurrentNode->Signature.Inputs={FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(),TEXT("Dependency"))};
    CurrentNode->Signature.Outputs={FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(),TEXT("Result"))};CurrentNode->AllocateDefaultPins();
    ContactInput->BreakLinkTo(ContactSource);ContactSource->MakeLinkTo(CurrentNode->FindPin(TEXT("Dependency"),EGPD_Input));CurrentNode->FindPin(TEXT("Result"),EGPD_Output)->MakeLinkTo(ContactInput);
    FindFProperty<FStrProperty>(CurrentNode->GetClass(),TEXT("CustomHlsl"))->SetPropertyValue_InContainer(CurrentNode,TEXT("Result=Dependency;\n#if GPU_SIMULATION\nGroupMemoryBarrier();\nResult=SharedNodePosition[GGroupThreadId.x];\n#endif\n"));
    UNiagaraNodeWithDynamicPins* Get=nullptr;UEdGraphPin* Destination=nullptr;UEdGraphPin* Position=nullptr;
    for(UEdGraphNode* N:Graph->Nodes)
    {
        if(auto* Call=Cast<UNiagaraNodeFunctionCall>(N);Call&&Call->FunctionScript==LoadObject<UNiagaraScript>(nullptr,TEXT("/HairStrands/Emitters/ExtractSharedPosition.ExtractSharedPosition")))
        {
            Position=Call->FindPin(TEXT("NodePosition"),EGPD_Output);
            if(Position&&Position->LinkedTo.Num()==1)Destination=Position->LinkedTo[0];
            auto* In=Call->FindPin(TEXT("NodePosition"),EGPD_Input);
            if(In&&In->LinkedTo.Num()==1)Get=Cast<UNiagaraNodeWithDynamicPins>(In->LinkedTo[0]->GetOwningNode());
        }
    }
    if(!Get||!Destination||!Position){Error=TEXT("Native Groom graph contract changed");return nullptr;}
    Position=CurrentNode->FindPin(TEXT("Result"),EGPD_Output);Destination=ContactInput;
    auto* H=NewObject<UNiagaraNodeCustomHlsl>(Graph);Graph->AddNode(H,false,false);H->CreateNewGuid();
    H->Signature.Inputs={FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(),TEXT("Current")),FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(),TEXT("Rest")),FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(),TEXT("Dt"))};
    if(SourceConstraints)H->Signature.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(UNiagaraDataInterfaceHairStrands::StaticClass()),TEXT("Hair")));
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
    FindFProperty<FStrProperty>(H->GetClass(),TEXT("CustomHlsl"))->SetPropertyValue_InContainer(H,FString::Printf(TEXT("Result = Current;\n#if GPU_SIMULATION\nfloat3 D = Current - Rest;\nfloat U = saturate(((float(GGroupThreadId.x %% %d)-1.0)) / %d.0);\nfloat T = U*U*(3.0-2.0*U);\nfloat W = 6.283185307 * lerp(%.9f, %.9f, T);\nfloat A = Dt*Dt*W*W;\nD /= 1.0+A;\nD *= min(1.0, %.9f / max(length(D), 1.e-6));\nResult = Rest+D;\nSharedNodePosition[GGroupThreadId.x]=Result;\nGroupMemoryBarrier();\n#endif\n"),NodeCount,NodeCount-2,Frequency,Kind==TEXT("loose")?1.2f:Frequency,Limit));
    if(SourceConstraints)
    {
        AddGet(TEXT("Emitter.HairStrands"),TEXT("Hair"));
        FString Body=HairSourceConstraintHlsl(SourceConstraints,Iterations,Error);if(!Error.IsEmpty())return nullptr;
        // Source support augments, rather than removes, the established rest-shape
        // regularizer. Free tips remain mobile; final body projection follows this.
        Body+=FString::Printf(TEXT("\nfloat U=saturate((float(GGroupThreadId.x %% %d)-1.0)/%d.0);float T=U*U*(3-2*U);float W=6.283185307*lerp(%.9f,%.9f,T);float3 D=(Result-Rest)/(1+Dt*Dt*W*W);Result=Rest+D*min(1.0,%.9f/max(length(D),1.e-6));\n"),NodeCount,NodeCount-2,Frequency,Kind==TEXT("loose")?1.2f:Frequency,Limit);
        FindFProperty<FStrProperty>(H->GetClass(),TEXT("CustomHlsl"))->SetPropertyValue_InContainer(H,TEXT("Result=Current;\n#if GPU_SIMULATION\n")+Body+TEXT("\nSharedNodePosition[GGroupThreadId.x]=Result;\nGroupMemoryBarrier();\n#endif\n"));
    }
    if(Kind!=TEXT("loose"))
    {
        // Tied bundles have a bounded attachment volume. Enforce the bound on
        // the final shared position too: native hard contact otherwise ejects
        // buried attachment paths after the material/support projection.
        UEdGraphPin* ReportInput=nullptr;
        for(UEdGraphNode* N:Graph->Nodes)if(auto* Call=Cast<UNiagaraNodeFunctionCall>(N);Call&&Call->FunctionScript&&Call->FunctionScript->GetName()==TEXT("ReportSharedPosition"))ReportInput=Call->FindPin(TEXT("NodePosition"),EGPD_Input);
        if(!ReportInput||ReportInput->LinkedTo.Num()!=1){Error=TEXT("Final support report contract changed");return nullptr;}
        auto* Previous=ReportInput->LinkedTo[0];
        auto* Final=NewObject<UNiagaraNodeCustomHlsl>(Graph);Graph->AddNode(Final,false,false);Final->CreateNewGuid();
        Final->Signature.Inputs={FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(),TEXT("Dependency")),FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(),TEXT("Rest"))};Final->Signature.Outputs={FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(),TEXT("Result"))};Final->AllocateDefaultPins();
        ReportInput->BreakLinkTo(Previous);Previous->MakeLinkTo(Final->FindPin(TEXT("Dependency"),EGPD_Input));Final->FindPin(TEXT("Result"),EGPD_Output)->MakeLinkTo(ReportInput);
        Get->FindPin(TEXT("Transient.TargetPosition"),EGPD_Output)->MakeLinkTo(Final->FindPin(TEXT("Rest"),EGPD_Input));
        FindFProperty<FStrProperty>(Final->GetClass(),TEXT("CustomHlsl"))->SetPropertyValue_InContainer(Final,FString::Printf(TEXT("Result=Dependency;\n#if GPU_SIMULATION\nGroupMemoryBarrier();\nfloat3 D=SharedNodePosition[GGroupThreadId.x]-Rest;\nResult=Rest+D*min(1.0,%.9f/max(length(D),1.e-6));\nSharedNodePosition[GGroupThreadId.x]=Result;\nGroupMemoryBarrier();\n#endif\n"),Limit));
    }
    static_cast<UNiagaraScriptSourceBase*>(Source)->MarkNotSynchronized(TEXT("Style constraint"));
    if(!VamHairBuild::Save(Copy)){Error=TEXT("Style module save failed");return nullptr;}
    auto* System=DuplicateObject<UNiagaraSystem>(Native,CreatePackage(*SystemPath),*FPackageName::GetLongPackageAssetName(SystemPath));System->SetFlags(RF_Public|RF_Standalone);
    int32 Replaced=0;
    TMap<UNiagaraScript*,UNiagaraScript*> Redirect;Redirect.Add(Project,Copy);
    {
        auto* Solve=LoadObject<UNiagaraScript>(nullptr,TEXT("/HairStrands/Emitters/SolveRodConstraints.SolveRodConstraints"));
        if(!Solve){Error=TEXT("Native solve module missing");return nullptr;}
        const FString SolvePath=Root/TEXT("SolveSupport_")+Key;
        auto* SolveCopy=DuplicateObject<UNiagaraScript>(Solve,CreatePackage(*SolvePath),*FPackageName::GetLongPackageAssetName(SolvePath));SolveCopy->SetFlags(RF_Public|RF_Standalone);
        if(!ConfigureHairContact(Copy,Kind!=TEXT("loose"),AnchorU)||!ConfigureHairContact(SolveCopy,Kind!=TEXT("loose"),AnchorU)){Error=TEXT("Support contact gating contract changed");return nullptr;}
        if(!VamHairBuild::Save(Copy)||!VamHairBuild::Save(SolveCopy)){Error=TEXT("Support module save failed");return nullptr;}
        Redirect.Add(Solve,SolveCopy);
    }
    TFunction<UNiagaraScript*(UNiagaraScript*)> Rewrite=[&](UNiagaraScript* Original)->UNiagaraScript*
    {
        if(!Original)return nullptr;if(auto** Known=Redirect.Find(Original))return *Known;
        Redirect.Add(Original,Original);
        auto* OS=Cast<UNiagaraScriptSource>(Original->GetLatestSource());if(!OS||!OS->NodeGraph)return Original;
        TMap<UNiagaraScript*,UNiagaraScript*> Changes;
        for(UEdGraphNode* N:OS->NodeGraph->Nodes)if(auto* Call=Cast<UNiagaraNodeFunctionCall>(N);Call&&Call->FunctionScript){auto* New=Rewrite(Call->FunctionScript);if(New!=Call->FunctionScript)Changes.Add(Call->FunctionScript,New);}
        if(Changes.IsEmpty())return Original;
        const FString P=Root/FString::Printf(TEXT("Style_%s_%08x_%s"),*Key,GetTypeHash(Original->GetPathName()),*Original->GetName());
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
    if(!System->IsValid()){Error=TEXT("Generated source constraint Niagara system failed compilation");return nullptr;}
    if(!VamHairBuild::Save(System)){Error=TEXT("Style solver save failed");return nullptr;}return System;
}

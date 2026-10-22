#pragma once

#include "NiagaraDataInterfacePhysicsAsset.h"

// Native ProjectHardCollisionConstraint resolves one primitive. Its correction
// can enter another primitive in a head/neck/torso union. Re-query only after
// that correction; normal-only continuation avoids applying friction repeatedly.
// This operates on simulation particles before publication, not rendered hair.
static bool AddHairUnionContact(UNiagaraScript* Script,FString& Error)
{
    auto* Source=Cast<UNiagaraScriptSource>(Script->GetLatestSource());auto* Graph=Source?Source->NodeGraph.Get():nullptr;if(!Graph)return false;
    UNiagaraNodeFunctionCall* Contact=nullptr;UNiagaraNodeFunctionCall* Query=nullptr;
    for(UEdGraphNode* Node:Graph->Nodes)if(auto* Call=Cast<UNiagaraNodeFunctionCall>(Node))
    {
        if(Call->Signature.Name==FName(TEXT("ProjectHardCollisionConstraint")))Contact=Call;
        if(Call->Signature.Name==FName(TEXT("GetClosestPoint")))Query=Call;
    }
    if(!Contact||!Query){Error=TEXT("Union contact requires native hard collision and closest query");return false;}
    auto* Position=Contact->FindPin(TEXT("Node Position"),EGPD_Output);
    if(!Position||Position->LinkedTo.Num()!=1){Error=TEXT("Union contact final output contract changed");return false;}
    auto* Destination=Position->LinkedTo[0];
    auto* H=NewObject<UNiagaraNodeCustomHlsl>(Graph);Graph->AddNode(H,false,false);H->CreateNewGuid();
    H->Signature.Inputs={
        FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(),TEXT("Current")),
        FNiagaraVariable(FNiagaraTypeDefinition(UNiagaraDataInterfacePhysicsAsset::StaticClass()),TEXT("Physics")),
        FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(),TEXT("Enabled")),
        FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(),TEXT("Clearance")),
        FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(),TEXT("FrameDt")),
        FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(),TEXT("Dt")),
        FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(),TEXT("TimeFraction"))};
    H->Signature.Outputs={FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(),TEXT("Result"))};H->AllocateDefaultPins();
    auto Link=[&](UNiagaraNodeFunctionCall* Call,const TCHAR* PinName,const TCHAR* Input){auto* Pin=Call->FindPin(PinName,EGPD_Input);auto* To=H->FindPin(Input,EGPD_Input);if(!Pin||Pin->LinkedTo.Num()!=1||!To)return false;Pin->LinkedTo[0]->MakeLinkTo(To);return true;};
    // Share the actual wired gate/clearance, including authored attachment
    // exemptions. Do not independently reinterpret a group as loose/attached.
    if(!Link(Contact,TEXT("Enable Constraint"),TEXT("Enabled"))||!Link(Contact,TEXT("Penetration Depth"),TEXT("Clearance"))||!Link(Contact,TEXT("Delta Time"),TEXT("Dt"))||!Link(Query,TEXT("Delta Time"),TEXT("FrameDt"))||!Link(Query,TEXT("Time Fraction"),TEXT("TimeFraction")))
    {Error=TEXT("Union contact native scalar input contract changed");return false;}
    UEdGraphPin* Physics=nullptr;for(auto* Pin:Query->Pins)if(Pin->Direction==EGPD_Input&&UEdGraphSchema_Niagara::PinToTypeDefinition(Pin)==FNiagaraTypeDefinition(UNiagaraDataInterfacePhysicsAsset::StaticClass()))Physics=Pin;
    if(!Physics||Physics->LinkedTo.Num()!=1){Error=TEXT("Union contact PhysicsAsset input contract changed");return false;}
    Physics->LinkedTo[0]->MakeLinkTo(H->FindPin(TEXT("Physics"),EGPD_Input));
    Position->BreakLinkTo(Destination);Position->MakeLinkTo(H->FindPin(TEXT("Current"),EGPD_Input));H->FindPin(TEXT("Result"),EGPD_Output)->MakeLinkTo(Destination);
    FindFProperty<FStrProperty>(H->GetClass(),TEXT("CustomHlsl"))->SetPropertyValue_InContainer(H,TEXT(R"HLSL(
Result=Current;
#if GPU_SIMULATION
if(Enabled && Dt>0 && GGroupThreadId.x%32>1)
{
    // Most nodes exit on the first query. Limit work on incompatible contacts;
    // pinned/author-supported nodes retain the upstream classification.
    for(int Pass=0;Pass<6;++Pass)
    {
        float3 Surface,Normal,Velocity;
        Physics.GetClosestPoint(Result,FrameDt,TimeFraction,Surface,Normal,Velocity);
        float Gap=dot(Result-Surface,Normal)-Clearance;
        if(Gap>=-0.0001 || dot(Normal,Normal)<0.5)break;
        Result-=Gap*Normal;
    }
    SharedNodePosition[GGroupThreadId.x]=Result;
}
GroupMemoryBarrier();
#endif
)HLSL"));
    static_cast<UNiagaraScriptSourceBase*>(Source)->MarkNotSynchronized(TEXT("Resolve overlapping body collision primitives after native projection"));return true;
}

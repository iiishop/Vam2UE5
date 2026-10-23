#pragma once

#include "NiagaraScript.h"
#include "NiagaraScriptSource.h"
#include "NiagaraGraph.h"
#include "NiagaraNodeCustomHlsl.h"
#include "NiagaraNodeFunctionCall.h"
#include "NiagaraNodeWithDynamicPins.h"
#include "EdGraphSchema_Niagara.h"
#include "Niagara/NiagaraDataInterfaceHairStrands.h"
#include "Misc/PackageName.h"
#include "VamHairSourceConstraints.h"

// AdvanceGroomState executes Advect -> Solve -> Project once per physical
// substep. Put source support on the advected position so the subsequent rod
// solve can restore lengths and final contact can keep its output priority.
// The caller redirects the native AdvectGroomState dependency to this copy and
// must omit the same source support from its ProjectGroomState copy.
// Included after VamHairBuild::Save, like the other private solver builders.
static UNiagaraScript* BuildHairPredictionSupport(
    const FString& Root,
    const FString& Key,
    const TSharedPtr<FJsonObject>& SourceConstraints,
    int32 Iterations,
    FString& Error)
{
    if (!SourceConstraints)
    {
        Error = TEXT("Prediction support requires source constraint data");
        return nullptr;
    }
    FString ConstraintError;
    const FString Body = HairSourceConstraintHlsl(SourceConstraints, Iterations, ConstraintError);
    if (!ConstraintError.IsEmpty() || Body.IsEmpty())
    {
        Error = TEXT("Prediction support source data: ") + ConstraintError;
        return nullptr;
    }

    auto* Native = LoadObject<UNiagaraScript>(nullptr,
        TEXT("/HairStrands/Emitters/AdvectGroomState.AdvectGroomState"));
    if (!Native)
    {
        Error = TEXT("Native AdvectGroomState module missing");
        return nullptr;
    }
    const FString Path = Root / (TEXT("PredictSource_") + Key);
    auto* Copy = DuplicateObject<UNiagaraScript>(Native, CreatePackage(*Path),
        *FPackageName::GetLongPackageAssetName(Path));
    if (!Copy)
    {
        Error = TEXT("Prediction support module duplication failed");
        return nullptr;
    }
    Copy->SetFlags(RF_Public | RF_Standalone);
    auto* Source = Cast<UNiagaraScriptSource>(Copy->GetLatestSource());
    auto* Graph = Source ? Source->NodeGraph.Get() : nullptr;
    if (!Graph)
    {
        Error = TEXT("Native AdvectGroomState graph missing");
        return nullptr;
    }

    UNiagaraNodeFunctionCall* Advect = nullptr;
    int32 AdvectCount = 0;
    for (UEdGraphNode* Node : Graph->Nodes)
    {
        auto* Call = Cast<UNiagaraNodeFunctionCall>(Node);
        if (Call && Call->Signature.Name == FName(TEXT("AdvectNodePosition")))
        {
            Advect = Call;
            ++AdvectCount;
        }
    }
    if (AdvectCount != 1)
    {
        Error = TEXT("Native AdvectNodePosition graph contract changed: expected one call");
        return nullptr;
    }
    auto* PositionOut = Advect->FindPin(TEXT("Node Position"), EGPD_Output);
    auto* PositionIn = Advect->FindPin(TEXT("Node Position"), EGPD_Input);
    if (!PositionOut || PositionOut->LinkedTo.Num() != 1 ||
        !PositionIn || PositionIn->LinkedTo.Num() != 1)
    {
        Error = TEXT("Native AdvectNodePosition position pin contract changed");
        return nullptr;
    }
    auto* Destination = PositionOut->LinkedTo[0];
    // MapGet/MapSet concrete classes are private to NiagaraEditor. Use their
    // public dynamic-pin base, while checking the native graph contract by name.
    auto* Get = Cast<UNiagaraNodeWithDynamicPins>(PositionIn->LinkedTo[0]->GetOwningNode());
    if (!Get || Get->GetClass()->GetFName() != FName(TEXT("NiagaraNodeParameterMapGet")) ||
        Destination->GetOwningNode()->GetClass()->GetFName() != FName(TEXT("NiagaraNodeParameterMapSet")) ||
        Destination->Direction != EGPD_Input ||
        Destination->PinName != FName(TEXT("Particles.NodePosition")))
    {
        Error = TEXT("Native advection parameter map contract changed");
        return nullptr;
    }

    auto* Hlsl = NewObject<UNiagaraNodeCustomHlsl>(Graph);
    Graph->AddNode(Hlsl, false, false);
    Hlsl->CreateNewGuid();
    Hlsl->Signature.Inputs = {
        FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Current")),
        FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Rest")),
        FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Dt")),
        FNiagaraVariable(FNiagaraTypeDefinition(UNiagaraDataInterfaceHairStrands::StaticClass()), TEXT("Hair")),
        FNiagaraVariable(FNiagaraTypeDefinition(UNiagaraDataInterfaceArrayFloat4::StaticClass()), TEXT("SourceData")),
        FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("Mobile"))
    };
    Hlsl->Signature.Outputs = {
        FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Result"))
    };
    Hlsl->AllocateDefaultPins();
    BindHairSourceTable(Hlsl,Get);
    auto* Current = Hlsl->FindPin(TEXT("Current"), EGPD_Input);
    auto* Result = Hlsl->FindPin(TEXT("Result"), EGPD_Output);
    auto* CodeProperty = FindFProperty<FStrProperty>(Hlsl->GetClass(), TEXT("CustomHlsl"));
    // Serialized engine modules can still carry a legacy FVector pin struct
    // while a newly allocated HLSL node uses Niagara's float-vector struct.
    // Niagara's schema performs the supported LWC/friendly-struct conversion;
    // raw FEdGraphPinType equality also compares unrelated pin qualifiers.
    // Require the actual normalized type, not a float3-sized blind cast.
    auto PinDescription = [](const UEdGraphPin* Pin) -> FString
    {
        if (!Pin) return TEXT("<missing>");
        return FString::Printf(TEXT("%s.%s [Niagara=%s category=%s struct=%s container=%d]"),
            *Pin->GetOwningNode()->GetName(), *Pin->PinName.ToString(),
            *UEdGraphSchema_Niagara::PinToTypeDefinition(Pin).GetName(),
            *Pin->PinType.PinCategory.ToString(),
            *GetPathNameSafe(Pin->PinType.PinSubCategoryObject.Get()),
            int32(Pin->PinType.ContainerType));
    };
    auto IsVectorPosition = [](const UEdGraphPin* Pin) -> bool
    {
        return Pin && Pin->PinType.ContainerType == EPinContainerType::None &&
            UEdGraphSchema_Niagara::PinToTypeDefinition(Pin) == FNiagaraTypeDefinition::GetVec3Def();
    };
    if (!Current || !Result || !CodeProperty ||
        !IsVectorPosition(Current) || !IsVectorPosition(PositionOut) ||
        !IsVectorPosition(Result) || !IsVectorPosition(Destination))
    {
        Error = FString::Printf(TEXT("Prediction support HLSL position contract changed: current=%s; native=%s; result=%s; destination=%s; code-property=%d"),
            *PinDescription(Current), *PinDescription(PositionOut),
            *PinDescription(Result), *PinDescription(Destination), CodeProperty ? 1 : 0);
        return nullptr;
    }

    auto BindMapInput = [&](FName Parameter, FName Input) -> bool
    {
        auto* InputPin = Hlsl->FindPin(Input, EGPD_Input);
        if (!InputPin)
        {
            Error = TEXT("Prediction support input missing: ") + Input.ToString();
            return false;
        }
        auto* OutputPin = Get->FindPin(Parameter, EGPD_Output);
        if (!OutputPin)
        {
            OutputPin = Get->CreatePin(EGPD_Output, InputPin->PinType, Parameter);
            if (OutputPin) OutputPin->PersistentGuid = FGuid::NewGuid();
        }
        if (!OutputPin || OutputPin->PinType.ContainerType != EPinContainerType::None ||
            UEdGraphSchema_Niagara::PinToTypeDefinition(OutputPin) !=
                UEdGraphSchema_Niagara::PinToTypeDefinition(InputPin))
        {
            Error = FString::Printf(TEXT("Prediction support parameter type mismatch: %s; output=%s; input=%s"),
                *Parameter.ToString(), *PinDescription(OutputPin), *PinDescription(InputPin));
            return false;
        }
        OutputPin->MakeLinkTo(InputPin);
        return true;
    };
    if (!BindMapInput(TEXT("Transient.TargetPosition"), TEXT("Rest")) ||
        !BindMapInput(TEXT("Emitter.StepTime"), TEXT("Dt")) ||
        !BindMapInput(TEXT("Emitter.HairStrands"), TEXT("Hair")) ||
        !BindMapInput(TEXT("Transient.IsPositionMobile"), TEXT("Mobile")))
    {
        return nullptr;
    }

    // Native root interpolation owns immobile nodes. Do not replace its
    // substep root target with the end-of-frame target. No shared-memory access
    // belongs here: SolveGroomState extracts this parameter-map position next.
    CodeProperty->SetPropertyValue_InContainer(Hlsl,
        TEXT("Result=Current;\n#if GPU_SIMULATION\nif(Mobile)\n{\n") +
        Body + TEXT("\n}\n#endif\n"));
    PositionOut->BreakLinkTo(Destination);
    PositionOut->MakeLinkTo(Current);
    Result->MakeLinkTo(Destination);
    static_cast<UNiagaraScriptSourceBase*>(Source)->MarkNotSynchronized(
        TEXT("Source support after advection, before rod constraints"));
    if (!VamHairBuild::Save(Copy))
    {
        Error = TEXT("Prediction support module save failed");
        return nullptr;
    }
    return Copy;
}

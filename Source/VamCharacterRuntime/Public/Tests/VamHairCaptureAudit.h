#pragma once
#include "Dom/JsonObject.h"
#include "VamHairComponent.h"
#include "GroomComponent.h"
#include "GroomAsset.h"
#include "GroomResources.h"
#include "RHIGPUReadback.h"
#include "NiagaraComponent.h"

// Explicit capture-only GPU readback. Never called by the production character tick.
static TSharedPtr<FJsonObject> VamCaptureHairAudit(AVamCharacterActor* Actor)
{
    auto Out=MakeShared<FJsonObject>();auto* H=Actor->Hair.Get();Out->SetBoolField(TEXT("hair_component"),H!=nullptr);if(H)Out->SetStringField(TEXT("status"),H->Status);if(!H||!H->Groom)return Out;auto* G=H->Groom.Get();
    Out->SetStringField(TEXT("status"),H->Status);int Active=0;for(auto N:G->NiagaraComponents)if(N&&N->IsActive())++Active;Out->SetNumberField(TEXT("niagara_components"),Active);
    TArray<TSharedPtr<FJsonValue>> Groups;
    for(int32 I=0;I<G->GroomAsset->GetNumHairGroups();++I)
    {
        auto* R=G->GetGuideStrandsDeformedResource(I);auto* Rest=G->GetGuideStrandsRestResource(I);auto Row=MakeShared<FJsonObject>();
        Row->SetNumberField(TEXT("group"),I);Row->SetBoolField(TEXT("simulation"),G->IsSimulationEnable(I,0));
        if(R&&Rest&&R->GetBuffer(FHairStrandsDeformedResource::Current).Buffer)
        {
            const uint32 Count=Rest->GetPointCount(),Bytes=Count*sizeof(FPackedHairVertex);TArray<FPackedHairVertex> Data,RestData;Data.SetNumZeroed(Count);RestData.SetNumZeroed(Count);FVector Offset;bool Ready=false;
            ENQUEUE_RENDER_COMMAND(VamHairRead)([R,Rest,Bytes,&Data,&RestData,&Offset,&Ready](FRHICommandListImmediate& Cmd){
                FRHIGPUBufferReadback Readback(TEXT("VamHairAudit"));Readback.EnqueueCopy(Cmd,R->GetBuffer(FHairStrandsDeformedResource::Current).Buffer->GetRHI(),Bytes);Cmd.SubmitAndBlockUntilGPUIdle();
                if(Readback.IsReady()){FMemory::Memcpy(Data.GetData(),Readback.Lock(Bytes),Bytes);Readback.Unlock();Ready=true;}
                FRHIGPUBufferReadback RestRead(TEXT("VamHairRestAudit"));RestRead.EnqueueCopy(Cmd,Rest->PositionBuffer.Buffer->GetRHI(),Bytes);Cmd.SubmitAndBlockUntilGPUIdle();if(RestRead.IsReady()){FMemory::Memcpy(RestData.GetData(),RestRead.Lock(Bytes),Bytes);RestRead.Unlock();}
                Offset=R->GetPositionOffset(FHairStrandsDeformedResource::Current);
            });FlushRenderingCommands();
            Row->SetBoolField(TEXT("readback_ready"),Ready);TArray<TSharedPtr<FJsonValue>> Points;if(Ready)for(const auto& P:Data){const FVector V=FVector(float(P.X),float(P.Y),float(P.Z))+Offset;Points.Add(MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>{MakeShared<FJsonValueNumber>(V.X),MakeShared<FJsonValueNumber>(V.Y),MakeShared<FJsonValueNumber>(V.Z)}));}
            TArray<TSharedPtr<FJsonValue>> RestPoints;for(const auto& P:RestData){const FVector V=FVector(float(P.X),float(P.Y),float(P.Z))+Rest->GetPositionOffset();RestPoints.Add(MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>{MakeShared<FJsonValueNumber>(V.X),MakeShared<FJsonValueNumber>(V.Y),MakeShared<FJsonValueNumber>(V.Z)}));}Row->SetArrayField(TEXT("guide_rest_points"),RestPoints);
            Row->SetNumberField(TEXT("guide_curves"),Rest->GetCurveCount());Row->SetArrayField(TEXT("guide_points"),Points);
        }
        Groups.Add(MakeShared<FJsonValueObject>(Row));
    }
    Out->SetArrayField(TEXT("groups"),Groups);return Out;
}


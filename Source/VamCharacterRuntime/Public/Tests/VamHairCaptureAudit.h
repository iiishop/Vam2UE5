#pragma once
#include "Dom/JsonObject.h"
#include "VamHairComponent.h"
#include "GroomComponent.h"
#include "GroomAsset.h"
#include "GroomResources.h"
#include "GroomInstance.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/SkeletalBodySetup.h"
#include "RHIGPUReadback.h"
#include "NiagaraComponent.h"

static TSharedPtr<FJsonObject> VamHairFrameDifference(const TSharedPtr<FJsonObject>& Current,const TSharedPtr<FJsonObject>& Previous)
{
    auto Out=MakeShared<FJsonObject>();TArray<TSharedPtr<FJsonValue>> Rows;
    if(!Previous.IsValid())return Out;
    const auto& Before=Previous->GetArrayField(TEXT("groups"));
    for(auto Value:Current->GetArrayField(TEXT("groups")))
    {
        auto G=Value->AsObject();const int I=G->GetIntegerField(TEXT("group"));if(!Before.IsValidIndex(I))continue;auto B=Before[I]->AsObject();
        auto Row=MakeShared<FJsonObject>();Row->SetNumberField(TEXT("group"),I);
        for(bool Render:{false,true})
        {
            const TCHAR* Key=Render?TEXT("render_points"):TEXT("guide_points");const TArray<TSharedPtr<FJsonValue>> *Now=nullptr,*Old=nullptr;
            if(!G->TryGetArrayField(Key,Now)||!B->TryGetArrayField(Key,Old))continue;
            TMap<int32,int32> OldIDs;if(Render){const auto& IDs=B->GetArrayField(TEXT("render_point_ids"));for(int J=0;J<IDs.Num();++J)OldIDs.Add(IDs[J]->AsNumber(),J);}
            double Sum=0,Max=0;int Count=0;
            for(int J=0;J<Now->Num();++J)
            {
                int K=J;if(Render){auto* Found=OldIDs.Find(G->GetArrayField(TEXT("render_point_ids"))[J]->AsNumber());if(!Found)continue;K=*Found;}if(!Old->IsValidIndex(K))continue;
                const auto& X=(*Now)[J]->AsArray();const auto& Y=(*Old)[K]->AsArray();double D=0;for(int Axis=0;Axis<3;++Axis)D+=FMath::Square(X[Axis]->AsNumber()-Y[Axis]->AsNumber());Sum+=D;Max=FMath::Max(Max,D);++Count;
            }
            const FString Prefix=Render?TEXT("render"):TEXT("guide");Row->SetNumberField(Prefix+TEXT("_max_step_cm"),FMath::Sqrt(Max));Row->SetNumberField(Prefix+TEXT("_rms_step_cm"),FMath::Sqrt(Sum/FMath::Max(1,Count)));Row->SetNumberField(Prefix+TEXT("_matched_points"),Count);
        }
        Rows.Add(MakeShared<FJsonValueObject>(Row));
    }
    Out->SetArrayField(TEXT("groups"),Rows);return Out;
}

// Explicit capture-only GPU readback. Never called by the production character tick.
static TSharedPtr<FJsonObject> VamCaptureHairAudit(AVamCharacterActor* Actor)
{
    auto Out=MakeShared<FJsonObject>();auto* H=Actor->Hair.Get();Out->SetBoolField(TEXT("hair_component"),H!=nullptr);if(H)Out->SetStringField(TEXT("status"),H->Status);if(!H||!H->Groom)return Out;auto* G=H->Groom.Get();
    Out->SetStringField(TEXT("status"),H->Status);int Active=0;for(auto N:G->NiagaraComponents)if(N&&N->IsActive())++Active;Out->SetNumberField(TEXT("niagara_components"),Active);
    Out->SetBoolField(TEXT("local_simulation"),G->SimulationSettings.SimulationSetup.bLocalSimulation);
    auto Vec=[](const FVector& V){return MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>{MakeShared<FJsonValueNumber>(V.X),MakeShared<FJsonValueNumber>(V.Y),MakeShared<FJsonValueNumber>(V.Z)});};
    TArray<TSharedPtr<FJsonValue>> Proxies;
    auto* Body=Actor->Character->Body.Get();
    auto Matrix=[](const FTransform& T){TArray<TSharedPtr<FJsonValue>> V;const FMatrix M=T.ToMatrixWithScale();for(int I=0;I<4;++I)for(int J=0;J<4;++J)V.Add(MakeShared<FJsonValueNumber>(M.M[I][J]));return V;};
    Out->SetArrayField(TEXT("groom_world_matrix"),Matrix(G->GetComponentTransform()));
    if(Body){Out->SetArrayField(TEXT("head_world_matrix"),Matrix(Body->GetSocketTransform(TEXT("head"))));Out->SetNumberField(TEXT("head_attachment_error_cm"),FVector::Distance(G->GetComponentLocation(),Body->GetSocketLocation(TEXT("head"))));}
    if(G->PhysicsAsset&&Body)for(USkeletalBodySetup* Setup:G->PhysicsAsset->SkeletalBodySetups)
    {
        const int32 Bone=Body->GetBoneIndex(Setup->BoneName);if(Bone<0)continue;
        auto Point=[&](const FVector& P){return G->GetComponentTransform().InverseTransformPosition(Body->GetBoneTransform(Bone).TransformPosition(P));};
        auto Add=[&](const FVector& A,const FVector& B,float R){auto O=MakeShared<FJsonObject>();O->SetStringField(TEXT("bone"),Setup->BoneName.ToString());O->SetArrayField(TEXT("a"),Vec(Point(A))->AsArray());O->SetArrayField(TEXT("b"),Vec(Point(B))->AsArray());O->SetNumberField(TEXT("radius_cm"),R);Proxies.Add(MakeShared<FJsonValueObject>(O));};
        for(const auto& S:Setup->AggGeom.SphereElems)Add(S.Center,S.Center,S.Radius);
        for(const auto& S:Setup->AggGeom.SphylElems){const FVector D=S.Rotation.RotateVector(FVector::UpVector)*S.Length*.5;Add(S.Center-D,S.Center+D,S.Radius);}
    }
    Out->SetArrayField(TEXT("collision_proxies_head_local"),Proxies);
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
        const auto& P=G->GroomAsset->GetHairGroupsPhysics()[I];
        Row->SetNumberField(TEXT("substeps"),P.SolverSettings.SubSteps);Row->SetNumberField(TEXT("iterations"),P.SolverSettings.IterationCount);
        Row->SetNumberField(TEXT("density"),P.StrandsParameters.StrandsDensity);Row->SetNumberField(TEXT("thickness_cm"),P.StrandsParameters.StrandsThickness);Row->SetNumberField(TEXT("bend_stiffness"),P.MaterialConstraints.BendConstraint.BendStiffness);
        Row->SetNumberField(TEXT("self_collision_viscosity"),P.MaterialConstraints.CollisionConstraint.StrandsViscosity);Row->SetNumberField(TEXT("air_drag"),P.ExternalForces.AirDrag);Row->SetStringField(TEXT("solver"),P.SolverSettings.CustomSystem.ToString());
        // Read the rendered strand buffer as well, with deterministic point IDs.
        auto* Instance=G->GetGroupInstance(I);
        if(Instance&&Instance->Strands.RestResource&&Instance->Strands.DeformedResource)
        {
            auto* RR=Instance->Strands.RestResource;auto* DR=Instance->Strands.DeformedResource;
            const uint32 Count=FMath::Min(RR->GetPointCount(),Instance->HairGroupPublicData->GetActiveStrandsPointCount()),Bytes=Count*sizeof(FPackedHairVertex);TArray<FPackedHairVertex> D,Base;D.SetNumZeroed(Count);Base.SetNumZeroed(Count);FVector Offset;bool Ready=false;
            ENQUEUE_RENDER_COMMAND(VamHairRenderRead)([DR,RR,Bytes,&D,&Base,&Offset,&Ready](FRHICommandListImmediate& Cmd){
                if(!DR->GetBuffer(FHairStrandsDeformedResource::Current).Buffer||!RR->PositionBuffer.Buffer)return;
                FRHIGPUBufferReadback A(TEXT("HairRenderedAudit")),B(TEXT("HairRenderedRestAudit"));
                A.EnqueueCopy(Cmd,DR->GetBuffer(FHairStrandsDeformedResource::Current).Buffer->GetRHI(),Bytes);B.EnqueueCopy(Cmd,RR->PositionBuffer.Buffer->GetRHI(),Bytes);Cmd.SubmitAndBlockUntilGPUIdle();
                if(A.IsReady()&&B.IsReady()){FMemory::Memcpy(D.GetData(),A.Lock(Bytes),Bytes);A.Unlock();FMemory::Memcpy(Base.GetData(),B.Lock(Bytes),Bytes);B.Unlock();Ready=true;}
                Offset=DR->GetPositionOffset(FHairStrandsDeformedResource::Current);
            });FlushRenderingCommands();
            Row->SetBoolField(TEXT("render_readback_ready"),Ready);Row->SetNumberField(TEXT("render_point_count"),Count);
            TArray<TSharedPtr<FJsonValue>> Render,RestRender,IDs;
            if(Ready)for(uint32 K=0;K<Count;K+=FMath::Max(1u,Count/2048u)){const auto& Q=D[K];const auto& T=Base[K];Render.Add(Vec(FVector(float(Q.X),float(Q.Y),float(Q.Z))+Offset));RestRender.Add(Vec(FVector(float(T.X),float(T.Y),float(T.Z))+RR->GetPositionOffset()));IDs.Add(MakeShared<FJsonValueNumber>(K));}
            Row->SetArrayField(TEXT("render_points"),Render);Row->SetArrayField(TEXT("render_rest_points"),RestRender);Row->SetArrayField(TEXT("render_point_ids"),IDs);
        }
        Groups.Add(MakeShared<FJsonValueObject>(Row));
    }
    Out->SetArrayField(TEXT("groups"),Groups);return Out;
}

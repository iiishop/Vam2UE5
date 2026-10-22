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
#include "NiagaraSystem.h"
#include "NiagaraSystemInstanceController.h"
#include "NiagaraDataInterfacePhysicsAsset.h"
#include "NiagaraEmitterInstance.h"
#include "NiagaraComputeExecutionContext.h"

// Capture-only diagnostics of the actual Niagara DI, rather than reconstructing
// proxies from Groom->PhysicsAsset and assuming the solver is using them.
static TSharedPtr<FJsonObject> VamCaptureHairPhysicsDI(UGroomComponent* Groom)
{
    auto Out=MakeShared<FJsonObject>();
    Out->SetStringField(TEXT("scope"),TEXT("Live Niagara PhysicsAsset DI after concurrent tick completion; current transform GPU buffer readback and independently resolved body bone positions. Capture-only GPU stall."));
    if(!IsInGameThread()||!Groom){Out->SetStringField(TEXT("status"),TEXT("error: requires game thread and a valid Groom"));return Out;}
    auto Vec=[](const FVector& V){return TArray<TSharedPtr<FJsonValue>>{MakeShared<FJsonValueNumber>(V.X),MakeShared<FJsonValueNumber>(V.Y),MakeShared<FJsonValueNumber>(V.Z)};};
    auto Matrix=[](const FTransform& T){TArray<TSharedPtr<FJsonValue>> R;const FMatrix M=T.ToMatrixWithScale();for(int I=0;I<4;++I)for(int J=0;J<4;++J)R.Add(MakeShared<FJsonValueNumber>(M.M[I][J]));return R;};
    FTransform SimulationTransform;Groom->BuildSimulationTransform(SimulationTransform);
    Out->SetArrayField(TEXT("simulation_to_world"),Matrix(SimulationTransform));
    TArray<TSharedPtr<FJsonValue>> Components;int32 FoundDI=0,Errors=0;
    for(int32 ComponentIndex=0;ComponentIndex<Groom->NiagaraComponents.Num();++ComponentIndex)
    {
        auto Row=MakeShared<FJsonObject>();Row->SetNumberField(TEXT("niagara_component_index"),ComponentIndex);
        UNiagaraComponent* N=Groom->NiagaraComponents[ComponentIndex];
        if(!N){Row->SetStringField(TEXT("status"),TEXT("error: null Niagara component"));++Errors;Components.Add(MakeShared<FJsonValueObject>(Row));continue;}
        Row->SetStringField(TEXT("component"),N->GetPathName());Row->SetStringField(TEXT("system"),GetPathNameSafe(N->GetAsset()));
        auto Controller=N->GetSystemInstanceController();
        if(!Controller.IsValid()||!Controller->IsValid()){Row->SetStringField(TEXT("status"),TEXT("error: no live system instance controller"));++Errors;Components.Add(MakeShared<FJsonValueObject>(Row));continue;}
        // The public controller performs the synchronization before the internal
        // instance accessor is used. No tick/reset/source update is invoked here.
        Controller->WaitForConcurrentTickAndFinalize();
        auto* Instance=Controller->GetSystemInstance_Unsafe();
        if(!Instance){Row->SetStringField(TEXT("status"),TEXT("error: controller has no system instance"));++Errors;Components.Add(MakeShared<FJsonValueObject>(Row));continue;}
        TArray<TSharedPtr<FJsonValue>> ParticleRows;
        for(const auto& Emitter:Instance->GetEmitters())
        {
            auto* GPU=Emitter->GetGPUContext();if(!GPU)continue;
            auto P=MakeShared<FJsonObject>();TArray<float> Floats;uint32 Stride=0,Count=0;bool Ready=false;
            ENQUEUE_RENDER_COMMAND(VamHairRawParticleRead)([GPU,&Floats,&Stride,&Count,&Ready](FRHICommandListImmediate& Cmd){
                auto* Buffer=GPU->GetDataToRender(Cmd,true);if(!Buffer||!Buffer->GetGPUBufferFloat().Buffer.IsValid())return;
                Stride=Buffer->GetFloatStride()/sizeof(float);Count=Buffer->GetNumInstances();const uint32 Bytes=Buffer->GetGPUBufferFloat().Buffer->GetSize();
                FRHIGPUBufferReadback Readback(TEXT("VamHairRawParticles"));Readback.EnqueueCopy(Cmd,Buffer->GetGPUBufferFloat().Buffer,Bytes);Cmd.SubmitAndBlockUntilGPUIdle();
                if(Readback.IsReady()){Floats.SetNumUninitialized(Bytes/sizeof(float));FMemory::Memcpy(Floats.GetData(),Readback.Lock(Bytes),Bytes);Readback.Unlock();Ready=true;}
            });FlushRenderingCommands();
            P->SetBoolField(TEXT("readback_ready"),Ready);P->SetNumberField(TEXT("instances"),Count);P->SetNumberField(TEXT("float_stride"),Stride);
            const auto& Data=Emitter->GetParticleData();const auto& Variables=Data.GetVariables();const auto& Layouts=Data.GetVariableLayouts();
            TArray<TSharedPtr<FJsonValue>> VariableNames;for(const auto& Variable:Variables)VariableNames.Add(MakeShared<FJsonValueString>(Variable.GetName().ToString()));P->SetArrayField(TEXT("available_variables"),VariableNames);
            for(int32 V=0;Ready&&V<Variables.Num();++V)
            {
                const FString StoredName=Variables[V].GetName().ToString();const FString Name=StoredName.StartsWith(TEXT("Particles."))?StoredName:TEXT("Particles.")+StoredName;
                const bool Vector=Name==TEXT("Particles.NodePosition")||Name==TEXT("Particles.RestPosition");
                const bool Scalar=Name==TEXT("Particles.RestLength");if(!Vector&&!Scalar)continue;
                const uint32 Start=Layouts[V].GetFloatComponentStart();const uint32 Width=Vector?3:1;
                if(!Stride||Count>Stride||(Start+Width)*Stride>uint32(Floats.Num())){P->SetStringField(TEXT("error"),TEXT("Raw particle layout exceeds readback"));continue;}
                TArray<TSharedPtr<FJsonValue>> Values;
                for(uint32 K=0;K<Count;++K){TArray<TSharedPtr<FJsonValue>> X;for(uint32 Axis=0;Axis<Width;++Axis)X.Add(MakeShared<FJsonValueNumber>(Floats[(Start+Axis)*Stride+K]));Values.Add(MakeShared<FJsonValueArray>(X));}
                P->SetArrayField(Name,Values);
            }
            ParticleRows.Add(MakeShared<FJsonValueObject>(P));
        }
        Row->SetArrayField(TEXT("raw_gpu_particles"),ParticleRows);
        TArray<TSharedPtr<FJsonValue>> Interfaces;
        for(int32 DIIndex=0;DIIndex<1024;++DIIndex)
        {
            UNiagaraDataInterface* Base=nullptr;void* Raw=nullptr;Instance->GetDataInterfaceInstanceDataInfo(DIIndex,Base,Raw);
            if(!Base&&!Raw)break;
            auto* DI=Cast<UNiagaraDataInterfacePhysicsAsset>(Base);if(!DI)continue;
            auto D=MakeShared<FJsonObject>();D->SetStringField(TEXT("interface"),DI->GetPathName());D->SetNumberField(TEXT("interface_index"),DIIndex);
            D->SetStringField(TEXT("mesh_user_parameter"),DI->MeshUserParameter.Parameter.GetName().ToString());D->SetStringField(TEXT("default_physics_asset"),GetPathNameSafe(DI->DefaultSource));
            auto* Data=Instance->FindTypedDataInterfaceInstanceData<FNDIPhysicsAssetData>(DI);
            if(!Data||Data!=Raw){D->SetStringField(TEXT("status"),TEXT("error: missing or inconsistent DI instance data"));++Errors;Interfaces.Add(MakeShared<FJsonValueObject>(D));continue;}
            ++FoundDI;const auto& A=Data->AssetArrays;const uint32 Count=A.ElementOffsets.NumElements;
            D->SetNumberField(TEXT("num_elements"),Count);D->SetNumberField(TEXT("sphere_begin"),A.ElementOffsets.SphereOffset);D->SetNumberField(TEXT("capsule_begin"),A.ElementOffsets.CapsuleOffset);D->SetNumberField(TEXT("tick_group"),int32(Data->TickingGroup));
            TArray<TSharedPtr<FJsonValue>> Sources;TMap<uint32,FVector> ExpectedSphereWorld;TMap<uint32,FString> SphereBones;TMap<uint32,int32> HeadOrdinal;uint32 SphereIndex=A.ElementOffsets.SphereOffset;
            for(int32 S=0;S<DI->PhysicsAssets.Num();++S)
            {
                auto SR=MakeShared<FJsonObject>();auto* Body=DI->SourceComponents.IsValidIndex(S)?DI->SourceComponents[S].Get():nullptr;auto* PA=DI->PhysicsAssets[S].Get();
                SR->SetStringField(TEXT("component"),GetPathNameSafe(Body));SR->SetStringField(TEXT("physics_asset"),GetPathNameSafe(PA));SR->SetBoolField(TEXT("live_skeletal_component"),Body!=nullptr);
                if(Body)SR->SetArrayField(TEXT("component_world_matrix"),Matrix(Body->GetComponentTransform()));
                Sources.Add(MakeShared<FJsonValueObject>(SR));if(!PA)continue;int32 HeadSphere=0;
                for(USkeletalBodySetup* Setup:PA->SkeletalBodySetups)
                {
                    if(!Setup)continue;const int32 Bone=Body?Body->GetBoneIndex(Setup->BoneName):INDEX_NONE;
                    // A null source uses the preview skeleton in the engine. Do
                    // not call that a live animated match or fabricate a center.
                    if(Body&&Bone==INDEX_NONE)continue;
                    for(const auto& Sphere:Setup->AggGeom.SphereElems)if(CollisionEnabledHasPhysics(Sphere.GetCollisionEnabled()))
                    {
                        SphereBones.Add(SphereIndex,Setup->BoneName.ToString());
                        if(Setup->BoneName==FName(TEXT("head")))HeadOrdinal.Add(SphereIndex,HeadSphere++);
                        if(Body&&Bone!=INDEX_NONE)ExpectedSphereWorld.Add(SphereIndex,Body->GetBoneTransform(Bone).TransformPosition(Sphere.Center));
                        ++SphereIndex;
                    }
                }
            }
            D->SetArrayField(TEXT("sources"),Sources);D->SetBoolField(TEXT("sphere_order_count_matches"),SphereIndex==A.ElementOffsets.CapsuleOffset);
            if(Count>=PHYSICS_ASSET_MAX_PRIMITIVES||A.ElementOffsets.CapsuleOffset>Count||A.ElementOffsets.SphereOffset>A.ElementOffsets.CapsuleOffset){D->SetStringField(TEXT("status"),TEXT("error: invalid DI primitive offsets"));++Errors;Interfaces.Add(MakeShared<FJsonValueObject>(D));continue;}
            TArray<FVector4f> GPUTransforms;GPUTransforms.SetNumZeroed(Count*3);bool GPUReady=false;auto* Buffer=Data->AssetBuffer;
            if(Count&&Buffer)
            {
                ENQUEUE_RENDER_COMMAND(VamHairPhysicsDIRead)([Buffer,Count,&GPUTransforms,&GPUReady](FRHICommandListImmediate& Cmd){
                    if(!Buffer->WorldTransformBuffer.Buffer.IsValid())return;
                    FRHIGPUBufferReadback Readback(TEXT("VamHairPhysicsDITransforms"));const uint32 Bytes=Count*3*sizeof(FVector4f);
                    Readback.EnqueueCopy(Cmd,Buffer->WorldTransformBuffer.Buffer,Bytes);Cmd.SubmitAndBlockUntilGPUIdle();
                    if(Readback.IsReady()){FMemory::Memcpy(GPUTransforms.GetData(),Readback.Lock(Bytes),Bytes);Readback.Unlock();GPUReady=true;}
                });FlushRenderingCommands();
            }
            D->SetBoolField(TEXT("gpu_current_transforms_readback_ready"),GPUReady);
            TArray<TSharedPtr<FJsonValue>> Elements;double MaxCPUError=0,MaxGPUError=0;int32 Matched=0;
            for(uint32 I=0;I<Count;++I)
            {
                auto E=MakeShared<FJsonObject>();E->SetNumberField(TEXT("index"),I);E->SetStringField(TEXT("type"),I<A.ElementOffsets.SphereOffset?TEXT("box"):I<A.ElementOffsets.CapsuleOffset?TEXT("sphere"):TEXT("capsule"));
                const FVector Center(A.CurrentTransform[I*3].W,A.CurrentTransform[I*3+1].W,A.CurrentTransform[I*3+2].W);const FVector Previous(A.PreviousTransform[I*3].W,A.PreviousTransform[I*3+1].W,A.PreviousTransform[I*3+2].W);
                E->SetArrayField(TEXT("current_center_simulation"),Vec(Center));E->SetArrayField(TEXT("previous_center_simulation"),Vec(Previous));E->SetArrayField(TEXT("extent"),Vec(FVector(A.ElementExtent[I].X,A.ElementExtent[I].Y,A.ElementExtent[I].Z)));E->SetNumberField(TEXT("physics_type"),A.PhysicsType[I]);
                if(const auto* Bone=SphereBones.Find(I))E->SetStringField(TEXT("bone"),*Bone);if(const auto* Ordinal=HeadOrdinal.Find(I))E->SetNumberField(TEXT("head_sphere_ordinal"),*Ordinal);
                if(GPUReady){const FVector GC(GPUTransforms[I*3].W,GPUTransforms[I*3+1].W,GPUTransforms[I*3+2].W);E->SetArrayField(TEXT("gpu_current_center_simulation"),Vec(GC));}
                if(const auto* Expected=ExpectedSphereWorld.Find(I))
                {
                    const double Error=FVector::Distance(SimulationTransform.TransformPosition(Center),*Expected);MaxCPUError=FMath::Max(MaxCPUError,Error);++Matched;E->SetArrayField(TEXT("expected_center_world_from_body"),Vec(*Expected));E->SetNumberField(TEXT("cpu_body_center_error_cm"),Error);
                    if(GPUReady){const FVector GC(GPUTransforms[I*3].W,GPUTransforms[I*3+1].W,GPUTransforms[I*3+2].W);const double GE=FVector::Distance(SimulationTransform.TransformPosition(GC),*Expected);MaxGPUError=FMath::Max(MaxGPUError,GE);E->SetNumberField(TEXT("gpu_body_center_error_cm"),GE);}
                }
                Elements.Add(MakeShared<FJsonValueObject>(E));
            }
            D->SetArrayField(TEXT("elements"),Elements);D->SetNumberField(TEXT("matched_live_spheres"),Matched);D->SetNumberField(TEXT("max_cpu_body_center_error_cm"),MaxCPUError);if(GPUReady)D->SetNumberField(TEXT("max_gpu_body_center_error_cm"),MaxGPUError);
            const bool Valid=Count>0&&GPUReady&&Matched>0&&SphereIndex==A.ElementOffsets.CapsuleOffset;
            D->SetStringField(TEXT("status"),Valid?TEXT("captured"):TEXT("error: empty primitives, unavailable GPU readback, missing live source, or sphere count mismatch"));if(!Valid)++Errors;Interfaces.Add(MakeShared<FJsonValueObject>(D));
        }
        Row->SetArrayField(TEXT("physics_asset_interfaces"),Interfaces);Row->SetStringField(TEXT("status"),Interfaces.Num()?TEXT("captured; inspect per-interface status"):TEXT("error: no live PhysicsAsset DI"));if(!Interfaces.Num())++Errors;Components.Add(MakeShared<FJsonValueObject>(Row));
    }
    Out->SetArrayField(TEXT("components"),Components);Out->SetNumberField(TEXT("interface_count"),FoundDI);Out->SetNumberField(TEXT("error_count"),Errors);Out->SetStringField(TEXT("status"),FoundDI&&Errors==0?TEXT("captured"):TEXT("error: one or more physics DI diagnostics unavailable"));return Out;
}

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
    Out->SetObjectField(TEXT("niagara_physics_di"),VamCaptureHairPhysicsDI(G));
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

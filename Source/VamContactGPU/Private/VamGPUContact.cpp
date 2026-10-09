#include "VamGPUContact.h"
#include "GlobalShader.h"
#include "ShaderParameterStruct.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RHIGPUReadback.h"
#include "RenderingThread.h"
#include "RHICommandList.h"
#include "HAL/PlatformTime.h"
#include "Algo/Sort.h"
class FVamGPUContactCS : public FGlobalShader
{
 DECLARE_GLOBAL_SHADER(FVamGPUContactCS);
 SHADER_USE_PARAMETER_STRUCT(FVamGPUContactCS,FGlobalShader);
 class FContactPass : SHADER_PERMUTATION_INT("VAM_CONTACT_PASS",37);
 using FPermutationDomain=TShaderPermutationDomain<FContactPass>;
 BEGIN_SHADER_PARAMETER_STRUCT(FParameters,)
  SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>,Positions)
  SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>,Rest)
  SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>,PreviousPositions)
  SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>,PreviousRest)
  SHADER_PARAMETER(uint32,PreviousOffset)
  SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint4>,ContactPairs)
  SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint4>,ContactTree)
  SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint4>,ContactFaces)
  SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint4>,ContactScenes)
  SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>,SurfaceMask)
  SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>,ContactBounds)
  SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>,PairHeads)
  SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>,PairNext)
  SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>,ContactActive)
  SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>,ContactDispatch)
  SHADER_PARAMETER(uint32,ContactNodeCount)
  SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>,WorldRows)
  SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>,LocalRows)
  SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>,SelectedPair)
  SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>,ElasticGradient)
  SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>,ContactLoads)
  SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>,LoadContributions)
  SHADER_PARAMETER(uint32,PairCount)
  SHADER_PARAMETER(uint32,LoadOffset)
  SHADER_PARAMETER(uint32,LoadInstance)
  SHADER_PARAMETER(uint32,SphereOffset)
  SHADER_PARAMETER(uint32,SphereCount)
  SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint4>,SkinHinges)
  SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint4>,SkinFaces)
  SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>,VertexOrder)
  SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>,Material)
  SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint4>,Tets)
  SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint2>,SurfaceEdges)
  SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float2>,EdgeLimits)
  SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>,Instance)
  SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>,Regions)
  SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>,VolumeGradients)
  SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>,ZoneState)
  SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>,ShapePlanes)
  SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>,ShapeRotations)
  SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>,ShapeExtents)
  SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>,Spheres)
  SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>,SphereStarts)
  SHADER_PARAMETER(float,ContactProgress)
  SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint2>,SphereRanges)
  SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>,Corrections)
  SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint2>,IncidentRanges)
  SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>,IncidentSlots)
  SHADER_PARAMETER(uint32,Offset)
  SHADER_PARAMETER(uint32,Count)
  SHADER_PARAMETER(uint32,TotalTets)
  SHADER_PARAMETER(uint32,TotalZones)
  SHADER_PARAMETER(uint32,TotalParticles)
  SHADER_PARAMETER(uint32,PassKind)
  SHADER_PARAMETER(float,EdgeRelaxation)
  SHADER_PARAMETER(float,VolumeRelaxation)
 END_SHADER_PARAMETER_STRUCT()
 static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& P){return IsFeatureLevelSupported(P.Platform,ERHIFeatureLevel::SM5);}
};
IMPLEMENT_GLOBAL_SHADER(FVamGPUContactCS,"/Plugin/VamContactGPU/Private/VamGPUContact.usf","MainCS",SF_Compute);
bool VamRunGPUContactExperiment(const FVamGPUContactInput& In,FVamGPUContactResult& Out)
{
 if(In.Rest.IsEmpty()||In.Tets.IsEmpty()||In.Instance.Num()!=In.Rest.Num()||In.Spheres.IsEmpty()){Out.Error=TEXT("Invalid input sizes");return false;}
 for(uint32 I:In.Instance)if(I>=uint32(In.Spheres.Num())){Out.Error=TEXT("Invalid instance index");return false;}
 TArray<TArray<int32>> VertexColors;VertexColors.SetNum(In.Rest.Num());TArray<TArray<FIntVector4>> Colors;
 for(const auto T:In.Tets)
 {
  for(int J=0;J<4;++J)if(T[J]<0||T[J]>=In.Rest.Num()){Out.Error=TEXT("Invalid tet");return false;}
  int Color=0;for(;;++Color){bool Used=false;for(int J=0;J<4;++J)Used|=VertexColors[T[J]].Contains(Color);if(!Used)break;}
  if(Color>=Colors.Num())Colors.SetNum(Color+1);Colors[Color].Add(T);for(int J=0;J<4;++J)VertexColors[T[J]].Add(Color);
 }
 TArray<FIntVector4> Ordered;TArray<FIntPoint> Ranges;
 for(const auto& C:Colors){Ranges.Add(FIntPoint(Ordered.Num(),C.Num()));Ordered.Append(C);}
 TArray<FIntPoint> IncidentRanges;TArray<uint32> IncidentSlots;TArray<TArray<uint32>> Incident;Incident.SetNum(In.Rest.Num());
 for(int T=0;T<Ordered.Num();++T)for(int J=0;J<4;++J)Incident[Ordered[T][J]].Add(T*4+J);
 for(const auto& List:Incident){IncidentRanges.Add(FIntPoint(IncidentSlots.Num(),List.Num()));IncidentSlots.Append(List);}
 Out.Colors=Colors.Num();Out.BufferBytes=uint64(In.Rest.Num())*(sizeof(FVector4f)*2+sizeof(uint32))+uint64(Ordered.Num())*sizeof(FIntVector4)+In.Spheres.Num()*sizeof(FVector4f);
 Out.BufferBytes+=uint64(Ordered.Num())*4*(sizeof(FVector4f)+sizeof(uint32))+uint64(In.Rest.Num())*sizeof(FIntPoint);
 ENQUEUE_RENDER_COMMAND(VamContactExperiment)([&In,&Out,Ordered=MoveTemp(Ordered),Ranges=MoveTemp(Ranges),IncidentRanges=MoveTemp(IncidentRanges),IncidentSlots=MoveTemp(IncidentSlots)](FRHICommandListImmediate& RHICmdList)
 {
  RHICmdList.SubmitAndBlockUntilGPUIdle();const double Start=FPlatformTime::Seconds();
  FRHIGPUBufferReadback Readback(TEXT("VamContactValidation"));
  FRDGBuilder G(RHICmdList);
  auto* Positions=CreateStructuredBuffer(G,TEXT("VamContact.Positions"),In.Rest);
  auto* Rest=CreateStructuredBuffer(G,TEXT("VamContact.Rest"),In.Rest);
  auto* Tets=CreateStructuredBuffer(G,TEXT("VamContact.Tets"),Ordered);
  const TArray<FIntPoint> DummyEdges={FIntPoint(0,0)};const TArray<FVector2f> DummyLimits={FVector2f(1,1)};
  auto* Edges=CreateStructuredBuffer(G,TEXT("VamContact.DummyEdges"),DummyEdges);auto* Limits=CreateStructuredBuffer(G,TEXT("VamContact.DummyLimits"),DummyLimits);
  auto* Instances=CreateStructuredBuffer(G,TEXT("VamContact.Instance"),In.Instance);
  TArray<FIntPoint> SR;for(int I=0;I<In.Spheres.Num();++I)SR.Add(FIntPoint(I,1));
  auto* SphereRanges=CreateStructuredBuffer(G,TEXT("VamContact.SphereRanges"),SR);
  TArray<FVector4f> LegacyRot,LegacyExt;LegacyRot.Init(FVector4f(0,0,0,1),In.Spheres.Num());LegacyExt.Init(FVector4f(0,0,0,0),In.Spheres.Num());
  auto* Rot=CreateStructuredBuffer(G,TEXT("VamContact.Rot"),LegacyRot);auto* Ext=CreateStructuredBuffer(G,TEXT("VamContact.Ext"),LegacyExt);
  auto* Spheres=CreateStructuredBuffer(G,TEXT("VamContact.Spheres"),In.Spheres);
  auto* Corrections=G.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(FVector4f),Ordered.Num()*4),TEXT("VamContact.Corrections"));
  auto* VGrad=G.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(FVector4f),In.Rest.Num()),TEXT("VamContact.VolumeGrad"));
  auto* ZState=G.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(FVector4f),In.Spheres.Num()*2),TEXT("VamContact.ZoneState"));
  auto* IncRanges=CreateStructuredBuffer(G,TEXT("VamContact.IncidentRanges"),IncidentRanges);
  auto* IncSlots=CreateStructuredBuffer(G,TEXT("VamContact.IncidentSlots"),IncidentSlots);
  auto Dispatch=[&](uint32 Kind,uint32 Offset,uint32 Count)
  {
   FVamGPUContactCS::FPermutationDomain Permutation;Permutation.Set<FVamGPUContactCS::FContactPass>(Kind);TShaderMapRef<FVamGPUContactCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel),Permutation);
   auto* P=G.AllocParameters<FVamGPUContactCS::FParameters>();P->Positions=G.CreateUAV(Positions);P->Rest=G.CreateSRV(Rest);P->PreviousPositions=G.CreateSRV(Rest);P->PreviousRest=G.CreateSRV(Rest);P->PreviousOffset=0;
   P->ContactPairs=G.CreateUAV(Tets);P->ContactTree=G.CreateSRV(Tets);P->ContactFaces=G.CreateSRV(Tets);P->ContactScenes=G.CreateSRV(Tets);P->SurfaceMask=G.CreateSRV(Instances);P->ContactBounds=G.CreateUAV(VGrad);P->PairHeads=G.CreateUAV(Instances);P->PairNext=G.CreateUAV(Instances);P->ContactActive=G.CreateUAV(Instances);P->ContactDispatch=G.CreateUAV(G.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(4,9),TEXT("Legacy.ContactDispatch")),PF_R32_UINT);P->ContactNodeCount=0;P->WorldRows=G.CreateSRV(Rest);P->LocalRows=G.CreateSRV(Rest);P->SelectedPair=G.CreateUAV(G.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32),In.Rest.Num()),TEXT("Legacy.Selected")));P->ElasticGradient=G.CreateUAV(VGrad);P->ContactLoads=G.CreateUAV(ZState);P->LoadContributions=G.CreateUAV(ZState);P->PairCount=0;P->LoadOffset=0;P->LoadInstance=0;P->SphereOffset=0;P->SphereCount=0;P->SkinHinges=G.CreateSRV(Tets);P->SkinFaces=G.CreateSRV(Tets);P->VertexOrder=G.CreateSRV(Instances);P->Material=G.CreateSRV(Rest);P->Tets=G.CreateSRV(Tets);P->SurfaceEdges=G.CreateSRV(Edges);P->EdgeLimits=G.CreateSRV(Limits);P->Instance=G.CreateSRV(Instances);P->Regions=G.CreateSRV(Instances);P->VolumeGradients=G.CreateUAV(VGrad);P->ZoneState=G.CreateUAV(ZState);P->ShapePlanes=G.CreateSRV(Ext);P->ShapeRotations=G.CreateSRV(Rot);P->ShapeExtents=G.CreateSRV(Ext);P->Spheres=G.CreateSRV(Spheres);P->SphereStarts=G.CreateSRV(Spheres);P->ContactProgress=1;P->SphereRanges=G.CreateSRV(SphereRanges);
   P->Corrections=G.CreateUAV(Corrections);P->IncidentRanges=G.CreateSRV(IncRanges);P->IncidentSlots=G.CreateSRV(IncSlots);
   P->Offset=Offset;P->Count=Count;P->TotalTets=Ordered.Num();P->TotalParticles=In.Rest.Num();P->TotalZones=In.Spheres.Num();P->PassKind=Kind;P->EdgeRelaxation=In.EdgeRelaxation;P->VolumeRelaxation=In.VolumeRelaxation;
   FComputeShaderUtils::AddPass(G,RDG_EVENT_NAME("VamGPUContact %u",Kind),Shader,P,FIntVector(FMath::DivideAndRoundUp(Count,64u),1,1));
  };
  for(int Iter=0;Iter<In.Iterations;++Iter){if(In.bJacobi){Dispatch(2,0,Ordered.Num());Dispatch(3,0,In.Rest.Num());}else{for(auto R:Ranges)Dispatch(0,R.X,R.Y);Dispatch(1,0,In.Rest.Num());}}
  AddEnqueueCopyPass(G,&Readback,Positions,In.Rest.Num()*sizeof(FVector4f));G.Execute();
  RHICmdList.ImmediateFlush(EImmediateFlushType::FlushRHIThread);RHICmdList.SubmitAndBlockUntilGPUIdle();
  if(!Readback.IsReady()){Out.Error=TEXT("Readback not ready after validation fence");return;}
  Out.Positions.SetNum(In.Rest.Num());const void* Data=Readback.Lock(In.Rest.Num()*sizeof(FVector4f));
  FMemory::Memcpy(Out.Positions.GetData(),Data,In.Rest.Num()*sizeof(FVector4f));Readback.Unlock();
  Out.SynchronizedWallMs=(FPlatformTime::Seconds()-Start)*1000;
 });
 FlushRenderingCommands();return Out.Error.IsEmpty()&&Out.Positions.Num()==In.Rest.Num();
}

#include "VamGPUContactState.h"
#include "Misc/ScopeLock.h"
#include "HAL/IConsoleManager.h"
namespace {
TMap<UObject*,FVamGPUContactHandlePtr> GPUHandles; // game thread registry
uint64 NextGPUId=1;
TAutoConsoleVariable<int32> GPUBarriers(TEXT("vam.Contact.GPUBarriers"),128,TEXT("Final active-set volume barrier iterations"));
TAutoConsoleVariable<int32> GPUIterations(TEXT("vam.Contact.GPUIterations"),8,TEXT("GPU corotated vertex-color material sweeps per active frame"));
TAutoConsoleVariable<int32> GPUDiagnostics(TEXT("vam.Contact.GPUDiagnostics"),1,TEXT("Asynchronous cage diagnostics every 30 batches; never blocks GPU"));
struct FGPUFrame {FVamGPUContactHandlePtr H;TArray<FVector4f> Rest,Spheres,Starts;FVamGPUContactScene Scene;};
struct FGPUBatchCache
{
 TArray<uint64> Ids;TArray<FIntPoint> Colors,VertexColors;
 TRefCountPtr<FRDGPooledBuffer> Tets,Instances,Ranges,Slots,Edges,Limits,EdgeRanges,EdgeSlots,Regions,Hinges,HingeRanges,HingeSlots,Faces,FaceRanges,FaceSlots,VertexOrder,Material;
 int32 TetCount=0,EdgeCount=0,HingeCount=0,FaceCount=0;uint64 Frame=0;
 TUniquePtr<FRHIGPUBufferReadback> Readback,LoadReadback,ContactReadback;
 TArray<FGPUFrame> LoadFrames;
 bool bLoadPending=false;double LoadSeconds=0;uint64 LoadSerial=0;
 TArray<FGPUFrame> ReadFrames;
 bool bReadPending=false;
 TRefCountPtr<FRDGPooledBuffer> ContactTree,ContactFaces,SurfaceMask;
 uint32 ContactNodeCount=0;

};
TUniquePtr<FGPUBatchCache> BatchCache; // render thread only
}
FVamGPUContactHandlePtr VamGPURegister(UObject* Mesh,FVamGPUContactTopology&& Topology)
{
 check(IsInGameThread());auto H=MakeShared<FVamGPUContactHandle,ESPMode::ThreadSafe>();
 H->Topology=MoveTemp(Topology);H->Id=NextGPUId++;GPUHandles.Add(Mesh,H);return H;
}
FVamGPUContactHandlePtr VamGPUFind(UObject* Mesh){check(IsInGameThread());return GPUHandles.FindRef(Mesh);}
void VamGPUUnregister(UObject* Mesh){check(IsInGameThread());GPUHandles.Remove(Mesh);}
void VamGPUUpdate(FVamGPUContactHandlePtr H,TArray<FVector4f>&& Rest,TArray<FVector4f>&& Spheres,TArray<FVector4f>&& Starts)
{check(IsInGameThread());if(H){H->InputRest=MoveTemp(Rest);H->InputSpheres=MoveTemp(Spheres);H->InputStarts=MoveTemp(Starts);H->bDirty=true;}}
FString VamGPUDiagnostics(FVamGPUContactHandlePtr H,double& Residual)
{
 if(!H)return TEXT("GPU inactive");FScopeLock Lock(&H->DiagnosticsMutex);Residual=0;
 for(int I=0;I<H->DiagnosticPositions.Num();++I)Residual=FMath::Max(Residual,double((FVector3f(H->DiagnosticPositions[I])-FVector3f(H->DiagnosticRest[I])).Size()));
 return FString::Printf(TEXT("GPU resident corotated + skin | batch %u instances / %u particles / %u GPU pair slots | scene upload prep %.3f ms | soft rounds %u active / %u searched, peak %u contacts | async diagnostic batch %llu | no synchronous readback"),H->BatchInstances,H->BatchParticles,H->BatchPairs,H->PairBuildMs,H->SoftActiveRounds,H->SoftSearches,H->SoftPeakContacts,H->DiagnosticFrame);
}
void VamGPUFlushBatch()
{
 check(IsInGameThread());TArray<FGPUFrame> Frames;
 for(auto& Pair:GPUHandles)if(Pair.Value->bDirty){auto H=Pair.Value;H->bDirty=false;Frames.Add({H,MoveTemp(H->InputRest),MoveTemp(H->InputSpheres),MoveTemp(H->InputStarts),H->InputScene});}
 if(Frames.IsEmpty())return;
 Frames.Sort([](const FGPUFrame& A,const FGPUFrame& B){return A.H->Id<B.H->Id;});
 const int Iterations=FMath::Clamp(GPUIterations.GetValueOnGameThread(),1,256);const bool Diagnostics=GPUDiagnostics.GetValueOnGameThread()!=0;const int Barriers=FMath::Clamp(GPUBarriers.GetValueOnGameThread(),0,512);
 ENQUEUE_RENDER_COMMAND(VamContactResidentBatch)([Frames=MoveTemp(Frames),Iterations,Diagnostics,Barriers](FRHICommandListImmediate& Cmd)
 {
  if(!BatchCache)BatchCache=MakeUnique<FGPUBatchCache>();auto& Cache=*BatchCache;++Cache.Frame;
  if(Cache.bReadPending&&Cache.Readback->IsReady()&&Cache.ContactReadback->IsReady())
  {
   const auto* ContactStats=static_cast<const uint32*>(Cache.ContactReadback->Lock(4*sizeof(uint32)));
   int Total=0;for(const auto& F:Cache.ReadFrames)Total+=F.Rest.Num();
   const auto* Data=static_cast<const FVector4f*>(Cache.Readback->Lock(Total*sizeof(FVector4f)));int Offset=0;
   for(const auto& F:Cache.ReadFrames){FScopeLock Lock(&F.H->DiagnosticsMutex);F.H->DiagnosticPositions.Reset();F.H->DiagnosticPositions.Append(Data+Offset,F.Rest.Num());F.H->DiagnosticRest=F.Rest;F.H->DiagnosticFrame=Cache.Frame;F.H->SoftSearches=ContactStats[1];F.H->SoftPeakContacts=ContactStats[2];F.H->SoftActiveRounds=ContactStats[3];Offset+=F.Rest.Num();}
   Cache.Readback->Unlock();Cache.ContactReadback->Unlock();Cache.bReadPending=false;Cache.ReadFrames.Reset();
  }
  if(Cache.bLoadPending && Cache.LoadReadback->IsReady()){
   int Total=0;for(const auto& F:Cache.LoadFrames)Total+=(F.Spheres.Num()+1)*2;
   const auto* Data=static_cast<const FVector4f*>(Cache.LoadReadback->Lock(Total*sizeof(FVector4f)));int Offset=0;
   for(const auto& F:Cache.LoadFrames){FScopeLock Lock(&F.H->DiagnosticsMutex);F.H->Loads.Reset();
    for(int I=0;I<=F.Spheres.Num();++I){FVamGPUContactLoad L;L.ColliderId=F.Scene.ColliderIds.IsValidIndex(I)?F.Scene.ColliderIds[I]:0;L.bSoftPair=I==F.Spheres.Num();L.Origin=F.Scene.BodyToWorld.GetLocation();L.ForceNewtons=FVector(FVector3f(Data[Offset++]));L.TorqueNewtonMeters=FVector(FVector3f(Data[Offset++]));if(!L.ForceNewtons.ContainsNaN()&&!L.TorqueNewtonMeters.ContainsNaN())F.H->Loads.Add(L);}
    F.H->LoadFrame=Cache.LoadSerial;F.H->LoadSubmittedSeconds=Cache.LoadSeconds;}
   Cache.LoadReadback->Unlock();Cache.bLoadPending=false;Cache.LoadFrames.Reset();}
  TArray<uint64> Ids;TArray<uint32> PreviousOffsets;TArray<FVector4f> Rest,Spheres,Starts;TArray<FIntPoint> SphereRanges;
  for(const auto& F:Frames){Ids.Add(F.H->Id);PreviousOffsets.Add(F.H->Offset);F.H->Offset=Rest.Num();Rest.Append(F.Rest);SphereRanges.Add(FIntPoint(Spheres.Num(),F.Spheres.Num()));Spheres.Append(F.Spheres);Starts.Append(F.H->Positions && F.H->PreviousSpheres.Num()==F.Spheres.Num() && F.H->PreviousColliderIds==F.Scene.ColliderIds?F.H->PreviousSpheres:F.Starts);}
  const double PairStart=FPlatformTime::Seconds();
  TArray<FVector4f> WorldRows,LocalRows;TArray<FIntVector4> ContactScenes;uint32 TotalRegions=0;
  const FVector Origin=Frames[0].Scene.BodyToWorld.GetLocation();bool HasSoft=false;
  for(const auto& F:Frames){FTransform X=F.Scene.BodyToWorld;X.AddToTranslation(-Origin);const FMatrix Matrix=X.ToMatrixWithScale(),Inverse=Matrix.Inverse();
   for(int R=0;R<3;++R){WorldRows.Add(FVector4f(Matrix.M[0][R],Matrix.M[1][R],Matrix.M[2][R],Matrix.M[3][R]));LocalRows.Add(FVector4f(Inverse.M[0][R],Inverse.M[1][R],Inverse.M[2][R],Inverse.M[3][R]));}
   ContactScenes.Add(FIntVector4(F.Scene.WorldId,F.Scene.bSoftCollision?1:0,TotalRegions,FMath::Min(F.H->Topology.ReactionRegionCount,F.H->Topology.RegionCount)));TotalRegions+=F.H->Topology.RegionCount;HasSoft|=F.Scene.bSoftCollision;
  }
  // GPU-generated, bounded to one closest foreign triangle per source vertex.
  const int NumPairs=HasSoft?Rest.Num():0;
  const double PairMs=(FPlatformTime::Seconds()-PairStart)*1000;
  if(Spheres.IsEmpty()){Spheres.Add(FVector4f(0,0,0,0));Starts=Spheres;}
  FRDGBuilder G(Cmd);
  FRDGBufferRef ContactTree,ContactFaces,SurfaceMask,Tets,Instances,Ranges,Slots,Edges,Limits,EdgeRanges,EdgeSlots,Regions,Hinges,HingeRanges,HingeSlots,Faces,FaceRanges,FaceSlots,VertexOrder,Material;
  if(Cache.Ids!=Ids)
  {
   Cache.Ids=Ids;TArray<FIntPoint> EE;TArray<FVector2f> LL;TArray<FIntVector4> TT;TArray<uint32> II,SS,RegionIds;TArray<FIntPoint> RR;TArray<TArray<uint32>> Adj;Adj.SetNum(Rest.Num());
   for(int B=0;B<Frames.Num();++B){const auto& F=Frames[B];for(int I=0;I<F.Rest.Num();++I){II.Add(B);RegionIds.Add(ContactScenes[B].Z+F.H->Topology.Regions[I]);}for(auto T:F.H->Topology.Tets){for(int J=0;J<4;++J)T[J]+=F.H->Offset;TT.Add(T);}}
   TArray<TArray<int32>> Used;Used.SetNum(Rest.Num());TArray<TArray<FIntVector4>> Groups;
   for(auto T:TT){int Color=0;for(;;++Color){bool Taken=false;for(int J=0;J<4;++J)Taken|=Used[T[J]].Contains(Color);if(!Taken)break;}if(Color>=Groups.Num())Groups.SetNum(Color+1);Groups[Color].Add(T);for(int J=0;J<4;++J)Used[T[J]].Add(Color);}
   TT.Reset();Cache.Colors.Reset();for(const auto& Group:Groups){Cache.Colors.Add(FIntPoint(TT.Num(),Group.Num()));TT.Append(Group);}
   for(int T=0;T<TT.Num();++T)for(int J=0;J<4;++J)Adj[TT[T][J]].Add(T*4+J);
   for(const auto& A:Adj){RR.Add(FIntPoint(SS.Num(),A.Num()));SS.Append(A);}
   TArray<FIntVector4> HH;TArray<TArray<uint32>> HA;HA.SetNum(Rest.Num());TArray<FIntPoint> HR;TArray<uint32> HS;
   for(const auto& F:Frames)for(auto H:F.H->Topology.SkinHinges){for(int J=0;J<4;++J){H[J]+=F.H->Offset;HA[H[J]].Add(HH.Num()*4+J);}HH.Add(H);}
   for(const auto& A:HA){HR.Add(FIntPoint(HS.Num(),A.Num()));HS.Append(A);}Cache.HingeCount=HH.Num();
   Hinges=CreateStructuredBuffer(G,TEXT("ContactBatch.Hinges"),HH);HingeRanges=CreateStructuredBuffer(G,TEXT("ContactBatch.HingeRanges"),HR);HingeSlots=CreateStructuredBuffer(G,TEXT("ContactBatch.HingeSlots"),HS);
   G.QueueBufferExtraction(Hinges,&Cache.Hinges);G.QueueBufferExtraction(HingeRanges,&Cache.HingeRanges);G.QueueBufferExtraction(HingeSlots,&Cache.HingeSlots);
   Cache.TetCount=TT.Num();TArray<TArray<uint32>> EdgeAdj;EdgeAdj.SetNum(Rest.Num());TArray<FIntPoint> ER;TArray<uint32> ES;
   for(const auto& F:Frames){for(auto E:F.H->Topology.SurfaceEdges)EE.Add(FIntPoint(E.X+F.H->Offset,E.Y+F.H->Offset));LL.Append(F.H->Topology.EdgeLimits);}
   for(int E=0;E<EE.Num();++E){EdgeAdj[EE[E].X].Add(E*4);EdgeAdj[EE[E].Y].Add(E*4+1);}for(const auto& A:EdgeAdj){ER.Add(FIntPoint(ES.Num(),A.Num()));ES.Append(A);}Cache.EdgeCount=EE.Num();
   Edges=CreateStructuredBuffer(G,TEXT("ContactBatch.Edges"),EE);Limits=CreateStructuredBuffer(G,TEXT("ContactBatch.EdgeLimits"),LL);EdgeRanges=CreateStructuredBuffer(G,TEXT("ContactBatch.EdgeRanges"),ER);EdgeSlots=CreateStructuredBuffer(G,TEXT("ContactBatch.EdgeSlots"),ES);
   G.QueueBufferExtraction(Edges,&Cache.Edges);G.QueueBufferExtraction(Limits,&Cache.Limits);G.QueueBufferExtraction(EdgeRanges,&Cache.EdgeRanges);G.QueueBufferExtraction(EdgeSlots,&Cache.EdgeSlots);

   TArray<FIntVector4> FF;TArray<TArray<uint32>> FA;FA.SetNum(Rest.Num());TArray<FIntPoint> FR;TArray<uint32> FS;
   for(const auto& F:Frames)for(auto T:F.H->Topology.SkinFaces){for(int J=0;J<4;++J)T[J]+=F.H->Offset;FF.Add(T);}
   // Immutable stackless BVH topology; only rebuilt on batch membership changes.
   // Current-pose bounds and all collision candidate selection run on the GPU.
   TArray<FIntVector4> BF=FF,Nodes;TArray<uint32> SM;SM.Init(0,Rest.Num());
   for(auto Face:FF)for(int J=0;J<3;++J)SM[Face[J]]=1;
   auto Center=[&](const FIntVector4& Face){return (FVector3f(Rest[Face.X])+FVector3f(Rest[Face.Y])+FVector3f(Rest[Face.Z]))/3.f;};
   TFunction<void(int,int)> BuildTree=[&](int Begin,int End){const int Node=Nodes.Num();Nodes.Add(FIntVector4(Begin,End-Begin,0,0));
    if(End-Begin>4){FBox3f Box(ForceInit);for(int I=Begin;I<End;++I)Box+=Center(BF[I]);const FVector3f Size=Box.GetSize();const int Axis=Size.X>=Size.Y&&Size.X>=Size.Z?0:Size.Y>=Size.Z?1:2;
     Algo::Sort(MakeArrayView(BF.GetData()+Begin,End-Begin),[&](const FIntVector4& A,const FIntVector4& B){return Center(A)[Axis]<Center(B)[Axis];});
     const int Mid=(Begin+End)/2;BuildTree(Begin,Mid);BuildTree(Mid,End);Nodes[Node].W=1;}
    Nodes[Node].Z=Nodes.Num();};
   BF.Sort([&](const FIntVector4& A,const FIntVector4& B){return RegionIds[A.X]<RegionIds[B.X];});
   for(int Begin=0;Begin<BF.Num();){int End=Begin+1;while(End<BF.Num()&&RegionIds[BF[End].X]==RegionIds[BF[Begin].X])++End;BuildTree(Begin,End);Begin=End;}Cache.ContactNodeCount=Nodes.Num();
   if(Nodes.IsEmpty())Nodes.Add(FIntVector4(0,0,1,0));if(BF.IsEmpty())BF.Add(FIntVector4(0,0,0,0));
   ContactTree=CreateStructuredBuffer(G,TEXT("ContactBatch.BVH"),Nodes);ContactFaces=CreateStructuredBuffer(G,TEXT("ContactBatch.BVHFaces"),BF);SurfaceMask=CreateStructuredBuffer(G,TEXT("ContactBatch.SurfaceMask"),SM);
   G.QueueBufferExtraction(ContactTree,&Cache.ContactTree);G.QueueBufferExtraction(ContactFaces,&Cache.ContactFaces);G.QueueBufferExtraction(SurfaceMask,&Cache.SurfaceMask);
   Cache.FaceCount=FF.Num();for(int I=0;I<FF.Num();++I)for(int J=0;J<3;++J)FA[FF[I][J]].Add(I*4+J);
   for(const auto& A:FA){FR.Add(FIntPoint(FS.Num(),A.Num()));FS.Append(A);}
   Faces=CreateStructuredBuffer(G,TEXT("ContactBatch.SkinFaces"),FF);FaceRanges=CreateStructuredBuffer(G,TEXT("ContactBatch.FaceRanges"),FR);FaceSlots=CreateStructuredBuffer(G,TEXT("ContactBatch.FaceSlots"),FS);
   G.QueueBufferExtraction(Faces,&Cache.Faces);G.QueueBufferExtraction(FaceRanges,&Cache.FaceRanges);G.QueueBufferExtraction(FaceSlots,&Cache.FaceSlots);
   TArray<TArray<int32>> Neighbors;Neighbors.SetNum(Rest.Num());for(auto T:TT)for(int A=0;A<4;++A)for(int B=A+1;B<4;++B){Neighbors[T[A]].AddUnique(T[B]);Neighbors[T[B]].AddUnique(T[A]);}
   TArray<int> VC;VC.Init(-1,Rest.Num());TArray<TArray<uint32>> VG;
   for(int I=0;I<Rest.Num();++I){if(Rest[I].W==0)continue;TSet<int> Taken;for(int J:Neighbors[I])if(VC[J]>=0)Taken.Add(VC[J]);int C=0;while(Taken.Contains(C))++C;VC[I]=C;if(VG.Num()<=C)VG.SetNum(C+1);VG[C].Add(I);}
   TArray<uint32> VO;Cache.VertexColors.Reset();for(const auto& Group:VG){Cache.VertexColors.Add(FIntPoint(VO.Num(),Group.Num()));VO.Append(Group);}
   TArray<FVector4f> MM;for(const auto& F:Frames)MM.Append(F.H->Topology.Material);
   VertexOrder=CreateStructuredBuffer(G,TEXT("ContactBatch.VertexOrder"),VO);Material=CreateStructuredBuffer(G,TEXT("ContactBatch.Material"),MM);
   G.QueueBufferExtraction(VertexOrder,&Cache.VertexOrder);G.QueueBufferExtraction(Material,&Cache.Material);
   Regions=CreateStructuredBuffer(G,TEXT("ContactBatch.Regions"),RegionIds);G.QueueBufferExtraction(Regions,&Cache.Regions);
   Tets=CreateStructuredBuffer(G,TEXT("ContactBatch.Tets"),TT);Instances=CreateStructuredBuffer(G,TEXT("ContactBatch.Instances"),II);
   Ranges=CreateStructuredBuffer(G,TEXT("ContactBatch.Ranges"),RR);Slots=CreateStructuredBuffer(G,TEXT("ContactBatch.Slots"),SS);
   G.QueueBufferExtraction(Tets,&Cache.Tets);G.QueueBufferExtraction(Instances,&Cache.Instances);G.QueueBufferExtraction(Ranges,&Cache.Ranges);G.QueueBufferExtraction(Slots,&Cache.Slots);
  }
  else{ContactTree=G.RegisterExternalBuffer(Cache.ContactTree);ContactFaces=G.RegisterExternalBuffer(Cache.ContactFaces);SurfaceMask=G.RegisterExternalBuffer(Cache.SurfaceMask);VertexOrder=G.RegisterExternalBuffer(Cache.VertexOrder);Material=G.RegisterExternalBuffer(Cache.Material);Faces=G.RegisterExternalBuffer(Cache.Faces);FaceRanges=G.RegisterExternalBuffer(Cache.FaceRanges);FaceSlots=G.RegisterExternalBuffer(Cache.FaceSlots);Hinges=G.RegisterExternalBuffer(Cache.Hinges);HingeRanges=G.RegisterExternalBuffer(Cache.HingeRanges);HingeSlots=G.RegisterExternalBuffer(Cache.HingeSlots);Tets=G.RegisterExternalBuffer(Cache.Tets);Instances=G.RegisterExternalBuffer(Cache.Instances);Ranges=G.RegisterExternalBuffer(Cache.Ranges);Slots=G.RegisterExternalBuffer(Cache.Slots);Regions=G.RegisterExternalBuffer(Cache.Regions);Edges=G.RegisterExternalBuffer(Cache.Edges);Limits=G.RegisterExternalBuffer(Cache.Limits);EdgeRanges=G.RegisterExternalBuffer(Cache.EdgeRanges);EdgeSlots=G.RegisterExternalBuffer(Cache.EdgeSlots);}
  auto* PairsBuffer=G.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(FIntVector4),Rest.Num()),TEXT("ContactBatch.SoftPairs"));
  auto* SceneBuffer=CreateStructuredBuffer(G,TEXT("ContactBatch.Scenes"),ContactScenes);
  auto* Bounds=G.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(FVector4f),FMath::Max(1u,Cache.ContactNodeCount)*2),TEXT("ContactBatch.Bounds"));
  auto* PairHeads=G.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32),Rest.Num()),TEXT("ContactBatch.PairHeads"));
  auto* PairNext=G.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32),Rest.Num()*4),TEXT("ContactBatch.PairNext"));
  auto* Active=G.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32),4),TEXT("ContactBatch.Active"));
  AddClearUAVPass(G,G.CreateUAV(Active),0u);
  auto IndirectDesc=FRDGBufferDesc::CreateBufferDesc(sizeof(uint32),9);IndirectDesc.Usage|=BUF_DrawIndirect;
  auto* Indirect=G.CreateBuffer(IndirectDesc,TEXT("ContactBatch.Indirect"));
  auto* DispatchScratch=G.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32),9),TEXT("ContactBatch.DispatchScratch"));
  auto* WorldBuffer=CreateStructuredBuffer(G,TEXT("ContactBatch.World"),WorldRows);auto* LocalBuffer=CreateStructuredBuffer(G,TEXT("ContactBatch.Local"),LocalRows);
  auto* Selected=G.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32),Rest.Num()),TEXT("ContactBatch.SelectedPairs"));
  auto* Gradient=G.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(FVector4f),Rest.Num()),TEXT("ContactBatch.ElasticGradient"));
  int LoadCount=0;for(const auto& F:Frames)LoadCount+=(F.Spheres.Num()+1)*2;
  auto* LoadParts=G.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(FVector4f),LoadCount*Rest.Num()),TEXT("ContactBatch.LoadParts"));
  auto* Loads=G.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(FVector4f),LoadCount),TEXT("ContactBatch.ReactionLoads"));
  auto* Positions=CreateStructuredBuffer(G,TEXT("ContactBatch.Positions"),Rest);
  auto* RestBuffer=CreateStructuredBuffer(G,TEXT("ContactBatch.Rest"),Rest);
  TArray<FVector4f> ShapeRot,ShapeExt,ShapePlanes;for(const auto& F:Frames){const int PlaneBase=ShapePlanes.Num();ShapePlanes.Append(F.Scene.ShapePlanes);for(int I=0;I<F.Spheres.Num();++I){ShapeRot.Add(F.Scene.ShapeRotations.IsValidIndex(I)?F.Scene.ShapeRotations[I]:FVector4f(0,0,0,1));auto E=F.Scene.ShapeExtents.IsValidIndex(I)?F.Scene.ShapeExtents[I]:FVector4f(0,0,0,0);if(E.W>2.5f)E.X+=PlaneBase;ShapeExt.Add(E);}}
  if(ShapePlanes.IsEmpty())ShapePlanes.Add(FVector4f(0,0,0,0));auto* ShapePlaneBuffer=CreateStructuredBuffer(G,TEXT("ContactBatch.ConvexPlanes"),ShapePlanes);
  if(ShapeRot.IsEmpty()){ShapeRot.Add(FVector4f(0,0,0,1));ShapeExt.Add(FVector4f(0,0,0,0));}
  auto* ShapeRotBuffer=CreateStructuredBuffer(G,TEXT("ContactBatch.ShapeRot"),ShapeRot);auto* ShapeExtBuffer=CreateStructuredBuffer(G,TEXT("ContactBatch.ShapeExt"),ShapeExt);
  auto* SphereBuffer=CreateStructuredBuffer(G,TEXT("ContactBatch.Spheres"),Spheres);
  auto* SphereStarts=CreateStructuredBuffer(G,TEXT("ContactBatch.SphereStarts"),Starts);
  auto* SphereRangeBuffer=CreateStructuredBuffer(G,TEXT("ContactBatch.SphereRanges"),SphereRanges);
  auto* Corrections=G.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(FVector4f),FMath::Max(NumPairs,FMath::Max(FMath::Max(Cache.TetCount,Cache.EdgeCount),FMath::Max(Cache.HingeCount,Cache.FaceCount)))*4),TEXT("ContactBatch.Corrections"));
  auto* VGrad=G.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(FVector4f),Rest.Num()),TEXT("ContactBatch.VolumeGrad"));
  auto* ZState=G.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(FVector4f),TotalRegions),TEXT("ContactBatch.ZoneState"));
  float Progress=1;uint32 LoadOffset=0,LoadInstance=0;
  auto Dispatch=[&](uint32 Kind,int Offset,int Count,FVamGPUContactHandle* Previous=nullptr,uint32 PreviousOffset=0,int IndirectOffset=-1)
  {
   FVamGPUContactCS::FPermutationDomain Permutation;Permutation.Set<FVamGPUContactCS::FContactPass>(Kind);TShaderMapRef<FVamGPUContactCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel),Permutation);
   auto* P=G.AllocParameters<FVamGPUContactCS::FParameters>();P->ContactPairs=G.CreateUAV(PairsBuffer);P->ContactTree=G.CreateSRV(ContactTree);P->ContactFaces=G.CreateSRV(ContactFaces);P->ContactScenes=G.CreateSRV(SceneBuffer);P->SurfaceMask=G.CreateSRV(SurfaceMask);P->ContactBounds=G.CreateUAV(Bounds);P->PairHeads=G.CreateUAV(PairHeads);P->PairNext=G.CreateUAV(PairNext);P->ContactActive=G.CreateUAV(Active);P->ContactDispatch=G.CreateUAV(Kind==36?Indirect:DispatchScratch,PF_R32_UINT);P->ContactNodeCount=Cache.ContactNodeCount;P->WorldRows=G.CreateSRV(WorldBuffer);P->LocalRows=G.CreateSRV(LocalBuffer);P->SelectedPair=G.CreateUAV(Selected);P->ElasticGradient=G.CreateUAV(Gradient);P->ContactLoads=G.CreateUAV(Loads);P->LoadContributions=G.CreateUAV(LoadParts);P->PairCount=NumPairs;P->LoadOffset=LoadOffset;P->LoadInstance=LoadInstance;P->SphereOffset=SphereRanges[LoadInstance].X;P->SphereCount=SphereRanges[LoadInstance].Y;P->Positions=G.CreateUAV(Positions);P->Rest=G.CreateSRV(RestBuffer);P->PreviousPositions=G.CreateSRV(Previous?G.RegisterExternalBuffer(Previous->Positions):RestBuffer);P->PreviousRest=G.CreateSRV(Previous?G.RegisterExternalBuffer(Previous->Rest):RestBuffer);P->PreviousOffset=PreviousOffset;P->Tets=G.CreateSRV(Tets);P->SkinHinges=G.CreateSRV(Hinges);P->SkinFaces=G.CreateSRV(Faces);P->VertexOrder=G.CreateSRV(VertexOrder);P->Material=G.CreateSRV(Material);P->SurfaceEdges=G.CreateSRV(Edges);P->EdgeLimits=G.CreateSRV(Limits);P->Instance=G.CreateSRV(Instances);P->Regions=G.CreateSRV(Regions);P->VolumeGradients=G.CreateUAV(VGrad);P->ZoneState=G.CreateUAV(ZState);P->ShapePlanes=G.CreateSRV(ShapePlaneBuffer);P->ShapeRotations=G.CreateSRV(ShapeRotBuffer);P->ShapeExtents=G.CreateSRV(ShapeExtBuffer);P->Spheres=G.CreateSRV(SphereBuffer);P->SphereStarts=G.CreateSRV(SphereStarts);P->ContactProgress=Progress;P->SphereRanges=G.CreateSRV(SphereRangeBuffer);
   P->Corrections=G.CreateUAV(Corrections);P->IncidentRanges=G.CreateSRV((Kind==19||Kind==29)?FaceRanges:Kind==17?HingeRanges:Kind==7?EdgeRanges:Ranges);P->IncidentSlots=G.CreateSRV((Kind==19||Kind==29)?FaceSlots:Kind==17?HingeSlots:Kind==7?EdgeSlots:Slots);P->Offset=Offset;P->Count=Count;P->TotalTets=Cache.TetCount;P->TotalParticles=Rest.Num();P->TotalZones=TotalRegions;P->PassKind=Kind;P->EdgeRelaxation=.35f;P->VolumeRelaxation=.9f;
   if(IndirectOffset>=0)FComputeShaderUtils::AddPass(G,RDG_EVENT_NAME("VamContactGPU Active %u",Kind),Shader,P,Indirect,uint32(IndirectOffset));
   else FComputeShaderUtils::AddPass(G,RDG_EVENT_NAME("VamContactGPU Batch %d Kind %u",Frames.Num(),Kind),Shader,P,FIntVector(FMath::DivideAndRoundUp(P->Count,64u),1,1));
  }
  ;
  for(int I=0;I<Frames.Num();++I)if(Frames[I].H->Positions)Dispatch(24,Frames[I].H->Offset,Frames[I].Rest.Num(),Frames[I].H.Get(),PreviousOffsets[I]);
  auto SelectContacts=[&](){Dispatch(34,0,Cache.ContactNodeCount*64);Dispatch(35,0,Rest.Num());Dispatch(28,0,Rest.Num());};
  auto SoftContact=[&](){if(NumPairs==0)return;SelectContacts();Dispatch(36,0,1);
   // Reuse the selected local manifold for three repairs. With no actual
   // contact all repairs dispatch zero groups. Keep the original correction
   // budget for real contact instead of weakening it to gain benchmark FPS.
   for(int Repair=0;Repair<3;++Repair){Dispatch(25,0,NumPairs,nullptr,0,0);Dispatch(26,0,Rest.Num(),nullptr,0,0);Dispatch(20,0,Cache.TetCount,nullptr,0,12);Dispatch(21,0,TotalRegions*64,nullptr,0,24);Dispatch(32,0,Rest.Num(),nullptr,0,0);}};
  auto SafeSkin=[&](bool Bend){Dispatch(Bend?16:18,0,Bend?Cache.HingeCount:Cache.FaceCount);Dispatch(Bend?17:19,0,Rest.Num());Dispatch(20,0,Cache.TetCount);Dispatch(21,0,TotalRegions*64);Dispatch(22,0,Rest.Num());};
  for(int It=0;It<Iterations;++It){Progress=FMath::Min(1.f,float(It+1)/FMath::Max(1.f,FMath::FloorToFloat(Iterations*.7f)));for(auto C:Cache.VertexColors)Dispatch(23,C.X,C.Y);Dispatch(1,0,Rest.Num());if(It%2==0){SafeSkin(true);SafeSkin(false);SoftContact();}}
  Progress=1;for(int I=0;I<Barriers;++I){if(I%8==0){Dispatch(8,0,Cache.TetCount);Dispatch(9,0,Rest.Num());Dispatch(10,0,TotalRegions*64);Dispatch(11,0,Rest.Num());SafeSkin(true);SafeSkin(false);if(I%32==0)SoftContact();}Dispatch(6,0,Cache.EdgeCount);Dispatch(7,0,Rest.Num());Dispatch(4,0,Cache.TetCount);Dispatch(5,0,Rest.Num());}
  for(int I=0;I<64;++I){Dispatch(4,0,Cache.TetCount);Dispatch(5,0,Rest.Num());if(I%4==0){SafeSkin(false);if(I%16==0)SoftContact();}}
  if(!Cache.bLoadPending){Dispatch(27,0,Rest.Num());if(NumPairs>0)SelectContacts();
   for(int I=0;I<Frames.Num();++I){LoadInstance=I;Dispatch(29,0,(Frames[I].Spheres.Num()+1)*Rest.Num());LoadOffset+=(Frames[I].Spheres.Num()+1)*2;}
   Dispatch(33,0,(LoadCount/2)*64);
   if(!Cache.LoadReadback)Cache.LoadReadback=MakeUnique<FRHIGPUBufferReadback>(TEXT("ContactAsyncLoads"));
   AddEnqueueCopyPass(G,Cache.LoadReadback.Get(),Loads,LoadCount*sizeof(FVector4f));Cache.LoadFrames=Frames;Cache.LoadSeconds=FPlatformTime::Seconds();Cache.LoadSerial=Cache.Frame;Cache.bLoadPending=true;}
  for(const auto& F:Frames)
  {
   auto H=F.H;H->PreviousSpheres=F.Spheres;H->PreviousColliderIds=F.Scene.ColliderIds;
   if(!H->Parents){auto* P=CreateStructuredBuffer(G,TEXT("ContactBatch.Parents"),H->Topology.Parents);auto* W=CreateStructuredBuffer(G,TEXT("ContactBatch.Weights"),H->Topology.Weights);auto* M=CreateStructuredBuffer(G,TEXT("ContactBatch.Mask"),H->Topology.Mask);G.QueueBufferExtraction(P,&H->Parents);G.QueueBufferExtraction(W,&H->Weights);G.QueueBufferExtraction(M,&H->Mask);}
   G.QueueBufferExtraction(Positions,&H->Positions);G.QueueBufferExtraction(RestBuffer,&H->Rest);
   FScopeLock Lock(&H->DiagnosticsMutex);H->BatchParticles=Rest.Num();H->BatchInstances=Frames.Num();H->BatchPairs=NumPairs;H->PairBuildMs=PairMs;
  }
  if(Diagnostics&&!Cache.bReadPending&&Cache.Frame%30==0){if(!Cache.Readback)Cache.Readback=MakeUnique<FRHIGPUBufferReadback>(TEXT("ContactAsyncDiagnostics"));AddEnqueueCopyPass(G,Cache.Readback.Get(),Positions,Rest.Num()*sizeof(FVector4f));if(!Cache.ContactReadback)Cache.ContactReadback=MakeUnique<FRHIGPUBufferReadback>(TEXT("ContactAsyncWork"));AddEnqueueCopyPass(G,Cache.ContactReadback.Get(),Active,4*sizeof(uint32));Cache.ReadFrames=Frames;Cache.bReadPending=true;}
  G.Execute();
 });
}

uint64 VamGPUReadDiagnostic(FVamGPUContactHandlePtr H,TArray<FVector>& Rest,TArray<FVector>& Current)
{
 if(!H)return 0;FScopeLock Lock(&H->DiagnosticsMutex);Rest.Reset();Current.Reset();
 for(auto P:H->DiagnosticPositions)Current.Add(FVector(FVector3f(P)));for(auto P:H->DiagnosticRest)Rest.Add(FVector(FVector3f(P)));return H->DiagnosticFrame;
}
void VamGPUShutdown()
{
 GPUHandles.Empty();ENQUEUE_RENDER_COMMAND(VamContactGPUShutdown)([](FRHICommandListImmediate&){BatchCache.Reset();});FlushRenderingCommands();
}

void VamGPUSetScene(FVamGPUContactHandlePtr H,FVamGPUContactScene&& Scene){check(IsInGameThread());if(H)H->InputScene=MoveTemp(Scene);}
uint64 VamGPUReadLoads(FVamGPUContactHandlePtr H,TArray<FVamGPUContactLoad>& Loads,double& Seconds){if(!H)return 0;FScopeLock Lock(&H->DiagnosticsMutex);Loads=H->Loads;Seconds=H->LoadSubmittedSeconds;return H->LoadFrame;}

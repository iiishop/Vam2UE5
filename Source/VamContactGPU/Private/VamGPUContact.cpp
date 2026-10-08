#include "VamGPUContact.h"
#include "GlobalShader.h"
#include "ShaderParameterStruct.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RHIGPUReadback.h"
#include "RenderingThread.h"
#include "RHICommandList.h"
#include "HAL/PlatformTime.h"
class FVamGPUContactCS : public FGlobalShader
{
 DECLARE_GLOBAL_SHADER(FVamGPUContactCS);
 SHADER_USE_PARAMETER_STRUCT(FVamGPUContactCS,FGlobalShader);
 BEGIN_SHADER_PARAMETER_STRUCT(FParameters,)
  SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>,Positions)
  SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>,Rest)
  SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint4>,Tets)
  SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint2>,SurfaceEdges)
  SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float2>,EdgeLimits)
  SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>,Instance)
  SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>,Regions)
  SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>,VolumeGradients)
  SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>,ZoneState)
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
  auto* Spheres=CreateStructuredBuffer(G,TEXT("VamContact.Spheres"),In.Spheres);
  auto* Corrections=G.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(FVector4f),Ordered.Num()*4),TEXT("VamContact.Corrections"));
  auto* VGrad=G.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(FVector4f),In.Rest.Num()),TEXT("VamContact.VolumeGrad"));
  auto* ZState=G.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(FVector4f),In.Spheres.Num()*2),TEXT("VamContact.ZoneState"));
  auto* IncRanges=CreateStructuredBuffer(G,TEXT("VamContact.IncidentRanges"),IncidentRanges);
  auto* IncSlots=CreateStructuredBuffer(G,TEXT("VamContact.IncidentSlots"),IncidentSlots);
  TShaderMapRef<FVamGPUContactCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
  auto Dispatch=[&](uint32 Kind,uint32 Offset,uint32 Count)
  {
   auto* P=G.AllocParameters<FVamGPUContactCS::FParameters>();P->Positions=G.CreateUAV(Positions);P->Rest=G.CreateSRV(Rest);
   P->Tets=G.CreateSRV(Tets);P->SurfaceEdges=G.CreateSRV(Edges);P->EdgeLimits=G.CreateSRV(Limits);P->Instance=G.CreateSRV(Instances);P->Regions=G.CreateSRV(Instances);P->VolumeGradients=G.CreateUAV(VGrad);P->ZoneState=G.CreateUAV(ZState);P->Spheres=G.CreateSRV(Spheres);P->SphereStarts=G.CreateSRV(Spheres);P->ContactProgress=1;P->SphereRanges=G.CreateSRV(SphereRanges);
   P->Corrections=G.CreateUAV(Corrections);P->IncidentRanges=G.CreateSRV(IncRanges);P->IncidentSlots=G.CreateSRV(IncSlots);
   P->Offset=Offset;P->Count=Count;P->TotalTets=Ordered.Num();P->TotalParticles=In.Rest.Num();P->PassKind=Kind;P->EdgeRelaxation=In.EdgeRelaxation;P->VolumeRelaxation=In.VolumeRelaxation;
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
TAutoConsoleVariable<int32> GPUColored(TEXT("vam.Contact.GPUColored"),0,TEXT("Experimental colored GPU schedule"));
TAutoConsoleVariable<int32> GPUIterations(TEXT("vam.Contact.GPUIterations"),96,TEXT("GPU Jacobi iterations"));
TAutoConsoleVariable<int32> GPUDiagnostics(TEXT("vam.Contact.GPUDiagnostics"),1,TEXT("Asynchronous cage diagnostics every 30 batches; never blocks GPU"));
struct FGPUFrame {FVamGPUContactHandlePtr H;TArray<FVector4f> Rest,Spheres,Starts;};
struct FGPUBatchCache
{
 TArray<uint64> Ids;TArray<FIntPoint> Colors;
 TRefCountPtr<FRDGPooledBuffer> Tets,Instances,Ranges,Slots,Edges,Limits,EdgeRanges,EdgeSlots,Regions;
 int32 TetCount=0,EdgeCount=0;uint64 Frame=0;
 TUniquePtr<FRHIGPUBufferReadback> Readback;
 TArray<FGPUFrame> ReadFrames;
 bool bReadPending=false;
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
 return FString::Printf(TEXT("GPU resident Jacobi | batch %u instances / %u particles | async diagnostic batch %llu | no synchronous readback"),H->BatchInstances,H->BatchParticles,H->DiagnosticFrame);
}
void VamGPUFlushBatch()
{
 check(IsInGameThread());TArray<FGPUFrame> Frames;
 for(auto& Pair:GPUHandles)if(Pair.Value->bDirty){auto H=Pair.Value;H->bDirty=false;Frames.Add({H,MoveTemp(H->InputRest),MoveTemp(H->InputSpheres),MoveTemp(H->InputStarts)});}
 if(Frames.IsEmpty())return;
 Frames.Sort([](const FGPUFrame& A,const FGPUFrame& B){return A.H->Id<B.H->Id;});
 const int Iterations=FMath::Clamp(GPUIterations.GetValueOnGameThread(),1,256);const bool Diagnostics=GPUDiagnostics.GetValueOnGameThread()!=0;const bool Colored=GPUColored.GetValueOnGameThread()!=0;const int Barriers=FMath::Clamp(GPUBarriers.GetValueOnGameThread(),0,512);
 ENQUEUE_RENDER_COMMAND(VamContactResidentBatch)([Frames=MoveTemp(Frames),Iterations,Diagnostics,Colored,Barriers](FRHICommandListImmediate& Cmd)
 {
  if(!BatchCache)BatchCache=MakeUnique<FGPUBatchCache>();auto& Cache=*BatchCache;++Cache.Frame;
  if(Cache.bReadPending&&Cache.Readback->IsReady())
  {
   int Total=0;for(const auto& F:Cache.ReadFrames)Total+=F.Rest.Num();
   const auto* Data=static_cast<const FVector4f*>(Cache.Readback->Lock(Total*sizeof(FVector4f)));int Offset=0;
   for(const auto& F:Cache.ReadFrames){FScopeLock Lock(&F.H->DiagnosticsMutex);F.H->DiagnosticPositions.Reset();F.H->DiagnosticPositions.Append(Data+Offset,F.Rest.Num());F.H->DiagnosticRest=F.Rest;F.H->DiagnosticFrame=Cache.Frame;Offset+=F.Rest.Num();}
   Cache.Readback->Unlock();Cache.bReadPending=false;Cache.ReadFrames.Reset();
  }
  TArray<uint64> Ids;TArray<FVector4f> Rest,Spheres,Starts;TArray<FIntPoint> SphereRanges;
  for(const auto& F:Frames){Ids.Add(F.H->Id);F.H->Offset=Rest.Num();Rest.Append(F.Rest);SphereRanges.Add(FIntPoint(Spheres.Num(),F.Spheres.Num()));Spheres.Append(F.Spheres);Starts.Append(F.Starts);}
  if(Spheres.IsEmpty()){Spheres.Add(FVector4f(0,0,0,0));Starts=Spheres;}
  FRDGBuilder G(Cmd);
  FRDGBufferRef Tets,Instances,Ranges,Slots,Edges,Limits,EdgeRanges,EdgeSlots,Regions;
  if(Cache.Ids!=Ids)
  {
   Cache.Ids=Ids;TArray<FIntPoint> EE;TArray<FVector2f> LL;TArray<FIntVector4> TT;TArray<uint32> II,SS,RegionIds;TArray<FIntPoint> RR;TArray<TArray<uint32>> Adj;Adj.SetNum(Rest.Num());
   for(int B=0;B<Frames.Num();++B){const auto& F=Frames[B];for(int I=0;I<F.Rest.Num();++I){II.Add(B);RegionIds.Add(B*2+F.H->Topology.Regions[I]);}for(auto T:F.H->Topology.Tets){for(int J=0;J<4;++J)T[J]+=F.H->Offset;TT.Add(T);}}
   TArray<TArray<int32>> Used;Used.SetNum(Rest.Num());TArray<TArray<FIntVector4>> Groups;
   for(auto T:TT){int Color=0;for(;;++Color){bool Taken=false;for(int J=0;J<4;++J)Taken|=Used[T[J]].Contains(Color);if(!Taken)break;}if(Color>=Groups.Num())Groups.SetNum(Color+1);Groups[Color].Add(T);for(int J=0;J<4;++J)Used[T[J]].Add(Color);}
   TT.Reset();Cache.Colors.Reset();for(const auto& Group:Groups){Cache.Colors.Add(FIntPoint(TT.Num(),Group.Num()));TT.Append(Group);}
   for(int T=0;T<TT.Num();++T)for(int J=0;J<4;++J)Adj[TT[T][J]].Add(T*4+J);
   for(const auto& A:Adj){RR.Add(FIntPoint(SS.Num(),A.Num()));SS.Append(A);}
   Cache.TetCount=TT.Num();TArray<TArray<uint32>> EdgeAdj;EdgeAdj.SetNum(Rest.Num());TArray<FIntPoint> ER;TArray<uint32> ES;
   for(const auto& F:Frames){for(auto E:F.H->Topology.SurfaceEdges)EE.Add(FIntPoint(E.X+F.H->Offset,E.Y+F.H->Offset));LL.Append(F.H->Topology.EdgeLimits);}
   for(int E=0;E<EE.Num();++E){EdgeAdj[EE[E].X].Add(E*4);EdgeAdj[EE[E].Y].Add(E*4+1);}for(const auto& A:EdgeAdj){ER.Add(FIntPoint(ES.Num(),A.Num()));ES.Append(A);}Cache.EdgeCount=EE.Num();
   Edges=CreateStructuredBuffer(G,TEXT("ContactBatch.Edges"),EE);Limits=CreateStructuredBuffer(G,TEXT("ContactBatch.EdgeLimits"),LL);EdgeRanges=CreateStructuredBuffer(G,TEXT("ContactBatch.EdgeRanges"),ER);EdgeSlots=CreateStructuredBuffer(G,TEXT("ContactBatch.EdgeSlots"),ES);
   G.QueueBufferExtraction(Edges,&Cache.Edges);G.QueueBufferExtraction(Limits,&Cache.Limits);G.QueueBufferExtraction(EdgeRanges,&Cache.EdgeRanges);G.QueueBufferExtraction(EdgeSlots,&Cache.EdgeSlots);

   Regions=CreateStructuredBuffer(G,TEXT("ContactBatch.Regions"),RegionIds);G.QueueBufferExtraction(Regions,&Cache.Regions);
   Tets=CreateStructuredBuffer(G,TEXT("ContactBatch.Tets"),TT);Instances=CreateStructuredBuffer(G,TEXT("ContactBatch.Instances"),II);
   Ranges=CreateStructuredBuffer(G,TEXT("ContactBatch.Ranges"),RR);Slots=CreateStructuredBuffer(G,TEXT("ContactBatch.Slots"),SS);
   G.QueueBufferExtraction(Tets,&Cache.Tets);G.QueueBufferExtraction(Instances,&Cache.Instances);G.QueueBufferExtraction(Ranges,&Cache.Ranges);G.QueueBufferExtraction(Slots,&Cache.Slots);
  }
  else{Tets=G.RegisterExternalBuffer(Cache.Tets);Instances=G.RegisterExternalBuffer(Cache.Instances);Ranges=G.RegisterExternalBuffer(Cache.Ranges);Slots=G.RegisterExternalBuffer(Cache.Slots);Regions=G.RegisterExternalBuffer(Cache.Regions);Edges=G.RegisterExternalBuffer(Cache.Edges);Limits=G.RegisterExternalBuffer(Cache.Limits);EdgeRanges=G.RegisterExternalBuffer(Cache.EdgeRanges);EdgeSlots=G.RegisterExternalBuffer(Cache.EdgeSlots);}
  auto* Positions=CreateStructuredBuffer(G,TEXT("ContactBatch.Positions"),Rest);
  auto* RestBuffer=CreateStructuredBuffer(G,TEXT("ContactBatch.Rest"),Rest);
  auto* SphereBuffer=CreateStructuredBuffer(G,TEXT("ContactBatch.Spheres"),Spheres);
  auto* SphereStarts=CreateStructuredBuffer(G,TEXT("ContactBatch.SphereStarts"),Starts);
  auto* SphereRangeBuffer=CreateStructuredBuffer(G,TEXT("ContactBatch.SphereRanges"),SphereRanges);
  auto* Corrections=G.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(FVector4f),FMath::Max(Cache.TetCount,Cache.EdgeCount)*4),TEXT("ContactBatch.Corrections"));
  TShaderMapRef<FVamGPUContactCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
  auto* VGrad=G.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(FVector4f),Rest.Num()),TEXT("ContactBatch.VolumeGrad"));
  auto* ZState=G.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(FVector4f),Frames.Num()*2),TEXT("ContactBatch.ZoneState"));
  float Progress=1;
  auto Dispatch=[&](uint32 Kind,int Offset,int Count)
  {
   auto* P=G.AllocParameters<FVamGPUContactCS::FParameters>();P->Positions=G.CreateUAV(Positions);P->Rest=G.CreateSRV(RestBuffer);P->Tets=G.CreateSRV(Tets);P->SurfaceEdges=G.CreateSRV(Edges);P->EdgeLimits=G.CreateSRV(Limits);P->Instance=G.CreateSRV(Instances);P->Regions=G.CreateSRV(Regions);P->VolumeGradients=G.CreateUAV(VGrad);P->ZoneState=G.CreateUAV(ZState);P->Spheres=G.CreateSRV(SphereBuffer);P->SphereStarts=G.CreateSRV(SphereStarts);P->ContactProgress=Progress;P->SphereRanges=G.CreateSRV(SphereRangeBuffer);
   P->Corrections=G.CreateUAV(Corrections);P->IncidentRanges=G.CreateSRV(Kind==7?EdgeRanges:Ranges);P->IncidentSlots=G.CreateSRV(Kind==7?EdgeSlots:Slots);P->Offset=Offset;P->Count=Count;P->TotalTets=Cache.TetCount;P->TotalParticles=Rest.Num();P->PassKind=Kind;P->EdgeRelaxation=.35f;P->VolumeRelaxation=.9f;
   FComputeShaderUtils::AddPass(G,RDG_EVENT_NAME("VamContactGPU Batch %d Kind %u",Frames.Num(),Kind),Shader,P,FIntVector(FMath::DivideAndRoundUp(P->Count,64u),1,1));
  }
  ;
  for(int It=0;It<Iterations;++It){Progress=FMath::Min(1.f,float(It+1)/FMath::Max(1.f,Iterations*.7f));if(Colored){for(auto C:Cache.Colors)Dispatch(0,C.X,C.Y);Dispatch(1,0,Rest.Num());}else{Dispatch(2,0,Cache.TetCount);Dispatch(3,0,Rest.Num());}}
  Progress=1;for(int I=0;I<Barriers;++I){if(I%8==0){Dispatch(8,0,Cache.TetCount);Dispatch(9,0,Rest.Num());Dispatch(10,0,Frames.Num()*2*64);Dispatch(11,0,Rest.Num());}Dispatch(6,0,Cache.EdgeCount);Dispatch(7,0,Rest.Num());Dispatch(4,0,Cache.TetCount);Dispatch(5,0,Rest.Num());}
  // Finish the unilateral feasibility solve without competing surface projections.
  for(int I=0;I<64;++I){Dispatch(4,0,Cache.TetCount);Dispatch(5,0,Rest.Num());}
  for(const auto& F:Frames)
  {
   auto H=F.H;
   if(!H->Parents){auto* P=CreateStructuredBuffer(G,TEXT("ContactBatch.Parents"),H->Topology.Parents);auto* W=CreateStructuredBuffer(G,TEXT("ContactBatch.Weights"),H->Topology.Weights);auto* M=CreateStructuredBuffer(G,TEXT("ContactBatch.Mask"),H->Topology.Mask);G.QueueBufferExtraction(P,&H->Parents);G.QueueBufferExtraction(W,&H->Weights);G.QueueBufferExtraction(M,&H->Mask);}
   G.QueueBufferExtraction(Positions,&H->Positions);G.QueueBufferExtraction(RestBuffer,&H->Rest);
   FScopeLock Lock(&H->DiagnosticsMutex);H->BatchParticles=Rest.Num();H->BatchInstances=Frames.Num();
  }
  if(Diagnostics&&!Cache.bReadPending&&Cache.Frame%30==0){if(!Cache.Readback)Cache.Readback=MakeUnique<FRHIGPUBufferReadback>(TEXT("ContactAsyncDiagnostics"));AddEnqueueCopyPass(G,Cache.Readback.Get(),Positions,Rest.Num()*sizeof(FVector4f));Cache.ReadFrames=Frames;Cache.bReadPending=true;}
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

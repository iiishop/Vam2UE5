#pragma once
#include "VamGPUContact.h"
#include "RenderGraphResources.h"
#include "HAL/CriticalSection.h"
struct FVamGPUContactHandle
{
 double PairBuildMs=0;
 FVamGPUContactTopology Topology;
 TArray<FVector4f> InputRest,InputSpheres,InputStarts; // game thread only
 FVamGPUContactScene InputScene;
 TArray<FVamGPUContactLoad> Loads;uint64 LoadFrame=0;double LoadSubmittedSeconds=0;
 bool bDirty=false;
 uint64 Id=0;
 // Render thread only. Mesh providers retain the handle, never a mutable UObject.
 TRefCountPtr<FRDGPooledBuffer> Positions,Rest,Parents,Weights,Mask;
 uint32 Offset=0;
 TArray<uint64> PreviousColliderIds;
 TArray<FVector4f> PreviousSpheres; // render-thread warm-start contact trajectory
 FCriticalSection DiagnosticsMutex;
 TArray<FVector4f> DiagnosticPositions,DiagnosticRest;
 uint32 BatchParticles=0,BatchInstances=0,BatchPairs=0;
 uint32 SoftSearches=0,SoftActiveRounds=0,SoftPeakContacts=0;
 uint64 DiagnosticFrame=0;
};
FVamGPUContactHandlePtr VamGPUFind(UObject* Mesh);

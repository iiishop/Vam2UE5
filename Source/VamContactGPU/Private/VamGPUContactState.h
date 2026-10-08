#pragma once
#include "VamGPUContact.h"
#include "RenderGraphResources.h"
#include "HAL/CriticalSection.h"
struct FVamGPUContactHandle
{
 FVamGPUContactTopology Topology;
 TArray<FVector4f> InputRest,InputSpheres,InputStarts; // game thread only
 bool bDirty=false;
 uint64 Id=0;
 // Render thread only. Mesh providers retain the handle, never a mutable UObject.
 TRefCountPtr<FRDGPooledBuffer> Positions,Rest,Parents,Weights,Mask;
 uint32 Offset=0;
 FCriticalSection DiagnosticsMutex;
 TArray<FVector4f> DiagnosticPositions,DiagnosticRest;
 uint32 BatchParticles=0,BatchInstances=0;
 uint64 DiagnosticFrame=0;
};
FVamGPUContactHandlePtr VamGPUFind(UObject* Mesh);

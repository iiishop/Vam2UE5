#pragma once
#include "CoreMinimal.h"
// Experimental volume/edge projection backend, not the native GS constitutive law.
struct FVamGPUContactInput
{
 TArray<FVector4f> Rest; // xyz rest positions, w inverse mass
 TArray<FIntVector4> Tets;
 TArray<FVector4f> Spheres;
 TArray<uint32> Instance;
 bool bJacobi=false;
 int32 Iterations=24;
 float EdgeRelaxation=.35f;
 float VolumeRelaxation=.9f;
};
struct FVamGPUContactResult
{
 TArray<FVector4f> Positions;
 double SynchronizedWallMs=0;
 uint64 BufferBytes=0;
 int32 Colors=0;
 FString Error;
};
// Blocking DEVELOPMENT validation only. No per-iteration readbacks.
VAMCONTACTGPU_API bool VamRunGPUContactExperiment(const FVamGPUContactInput& Input,FVamGPUContactResult& Output);

// Resident runtime: corotated vertex-color material, skin and persistent per-instance state.
// The blocking legacy kernel above remains a volume/edge benchmark only.
struct VAMCONTACTGPU_API FVamGPUContactTopology
{
 TArray<FIntVector4> Tets, Parents, SkinHinges, SkinFaces;
 TArray<uint32> Regions;
 TArray<FIntPoint> SurfaceEdges;
 TArray<FVector2f> EdgeLimits;
 TArray<FVector4f> Weights, Material; // mu, lambda, foundation stiffness, bending
 TArray<float> Mask;
};
struct FVamGPUContactHandle;
using FVamGPUContactHandlePtr=TSharedPtr<FVamGPUContactHandle,ESPMode::ThreadSafe>;
VAMCONTACTGPU_API FVamGPUContactHandlePtr VamGPURegister(UObject* Mesh,FVamGPUContactTopology&& Topology);
VAMCONTACTGPU_API void VamGPUUnregister(UObject* Mesh);
VAMCONTACTGPU_API void VamGPUUpdate(FVamGPUContactHandlePtr Handle,TArray<FVector4f>&& Rest,TArray<FVector4f>&& Spheres,TArray<FVector4f>&& Starts);
VAMCONTACTGPU_API void VamGPUFlushBatch();
VAMCONTACTGPU_API FString VamGPUDiagnostics(FVamGPUContactHandlePtr Handle,double& Residual);
VAMCONTACTGPU_API uint64 VamGPUReadDiagnostic(FVamGPUContactHandlePtr Handle,TArray<FVector>& Rest,TArray<FVector>& Current);
VAMCONTACTGPU_API void VamGPUShutdown();

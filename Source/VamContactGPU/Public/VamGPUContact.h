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

/** Scene inputs are copied on the game thread. No UObject is accessed by the GPU. */
struct VAMCONTACTGPU_API FVamGPUContactScene
{
 FTransform BodyToWorld=FTransform::Identity;
 FTransform PairFrameToWorld=FTransform::Identity; // rigid chest frame, CPU broadphase cache only
 uint32 WorldId=0;
 bool bSoftCollision=false;
 TArray<FVector4f> ShapePlanes; // convex local planes n.xyz dot x <= w
 TArray<FVector4f> ShapeRotations,ShapeExtents; // quaternion; xyz box half extents or capsule half segment in z; w type sphere/box/capsule/convex 0/1/2/3
 TArray<uint64> ColliderIds; // one stable id per sphere; zero denotes a prescribed probe
};
struct VAMCONTACTGPU_API FVamGPUContactLoad
{
 uint64 ColliderId=0;
 FVector ForceNewtons=FVector::ZeroVector;
 FVector TorqueNewtonMeters=FVector::ZeroVector; // about BodyToWorld origin
 FVector Origin=FVector::ZeroVector;
 bool bSoftPair=false;
};
VAMCONTACTGPU_API void VamGPUSetScene(FVamGPUContactHandlePtr Handle,FVamGPUContactScene&& Scene);
// Latest completed asynchronous force sample; caller may hold it until next sample,
// but must expire stale data. These are forces, not impulses.
VAMCONTACTGPU_API uint64 VamGPUReadLoads(FVamGPUContactHandlePtr Handle,TArray<FVamGPUContactLoad>& Loads,double& SubmittedSeconds);

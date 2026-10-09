#pragma once
#include "CoreMinimal.h"
class UPrimitiveComponent;
class UBodySetup;
class UVamBreastSkeletalMeshComponent;
struct FVamContactRigidSource
{
 UPrimitiveComponent* Component=nullptr;
 UBodySetup* Setup=nullptr;
 FTransform World=FTransform::Identity;
 FName Bone;
 uint32 BodyTag=0;
};
// Resolve individual skeletal bodies; never use the skeletal component transform
// as if it were a hand/femur collision frame.
void VamGatherRigidSources(UPrimitiveComponent* Source,UVamBreastSkeletalMeshComponent* OwnBody,const FBox& Bounds,TArray<FVamContactRigidSource>& Out);
bool VamSupportsGPURigid(const FVamContactRigidSource& Source);

#pragma once
#include "CoreMinimal.h"
class UVamGluteSkeletalMeshComponent;
// On demand, Editor-only audit of native LOD0 at the current instance pose.
// Does not modify pose, Morph weights, source assets, or the runtime skin path.
FString CaptureGluteSurface(UVamGluteSkeletalMeshComponent& Body,bool Draw=true);

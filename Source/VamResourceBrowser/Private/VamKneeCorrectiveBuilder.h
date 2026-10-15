#pragma once
#include "VamGluteCorrectiveProfile.h"
#include "VamNativeBuilder.h"
namespace VamKneeBuilder
{
bool Build(FVamNativeMeshInput&,const TArray<int32>&,UVamGluteCorrectiveProfile&,const FString&,FString&);
}

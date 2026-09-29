#pragma once
#include "VamNativeBuilder.h"
#include "VamGluteStructure.h"
#include "VamGluteCorrectiveProfile.h"
namespace VamGluteCorrectiveBuilder
{
bool Build(FVamNativeMeshInput& Input,const TArray<FVamBuildInfluence>& OriginalInfluences,const TArray<int32>& SourceIds,const UVamGluteStructureProfile& Structure,UVamGluteCorrectiveProfile& Profile,const FString& FamilyJson,FString& Error);
}

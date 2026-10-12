#pragma once
#include "Animation/MorphTarget.h"
#include "VamMorphNormalUtils.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Rendering/SkeletalMeshLODModel.h"
#include "Rendering/SkeletalMeshRenderData.h"

// Negative control only: replace verified B2 source/procedural fields on a
// transient duplicate. No persistent mesh or validated profile is changed.
static bool VamInstallUnsafeMorphDiagnostic(FAutomationTestBase& Test,UVamLegSkeletalMeshComponent* Body,const FString& Path)
{
 FString Text;TSharedPtr<FJsonObject> Root;
 if(!FFileHelper::LoadFileToString(Text,*Path)||!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Root))return false;
 auto* Original=Body->GetSkeletalMeshAsset();
 const FString Config=Root->GetStringField(TEXT("configuration"));
 if(!Original->GetPathName().StartsWith(FPaths::GetPath(Config)))return false;
 const int Count=Root->GetIntegerField(TEXT("render_vertices"));
 if(Original->GetResourceForRendering()->LODRenderData[0].GetNumVertices()!=Count)return false;
 auto* Mesh=DuplicateObject<USkeletalMesh>(Original,GetTransientPackage());
 const auto& Render=Mesh->GetResourceForRendering()->LODRenderData[0];
 TArray<FVector> Positions,Normals;for(int V=0;V<Count;++V){Positions.Add(FVector(Render.StaticVertexBuffers.PositionVertexBuffer.VertexPosition(V)));Normals.Add(FVector(Render.StaticVertexBuffers.StaticMeshVertexBuffer.VertexTangentZ(V)));}
 TArray<int32> Triangles;const auto* Indices=Render.MultiSizeIndexContainer.GetIndexBuffer();for(int32 J=0;J<Indices->Num();++J)Triangles.Add(Indices->Get(J));
 Mesh->ReleaseResources();FlushRenderingCommands();
 for(const auto& Entry:Root->GetArrayField(TEXT("morphs")))
 {
  const auto O=Entry->AsObject();const FName Name(*O->GetStringField(TEXT("name")));TArray<uint8> Bytes;
  if(!FFileHelper::LoadFileToArray(Bytes,*O->GetStringField(TEXT("file")))||Bytes.Num()!=Count*3*sizeof(float))return false;
  auto* Target=Mesh->FindMorphTarget(Name);
  if(!Target||Target->GetOuter()!=Mesh)return false;
  TArray<FMorphTargetDelta> Deltas;const float* Data=reinterpret_cast<const float*>(Bytes.GetData());
  TArray<FVector> Displacements;for(int V=0;V<Count;++V)Displacements.Add(FVector(Data[V*3],Data[V*3+1],Data[V*3+2]));
  const auto NormalDeltas=VamMorphNormals::Deltas(Positions,Normals,Triangles,Displacements);
  for(int V=0;V<Count;++V){FMorphTargetDelta D;D.SourceIdx=V;D.PositionDelta=FVector3f(Data[V*3],Data[V*3+1],Data[V*3+2]);D.TangentZDelta=FVector3f(NormalDeltas[V]);if(D.PositionDelta.ContainsNaN())return false;if(!D.PositionDelta.IsNearlyZero(1.e-8f))Deltas.Add(D);}
  Target->PopulateDeltas(Deltas,0,Mesh->GetImportedModel()->LODModels[0].Sections,true,false,0.f);
  Mesh->RegisterMorphTarget(Target,false);
 }
 // Do not PostEditChange/rebuild from imported source: that restores the old
 // Morph payload. Rebuild this duplicate's GPU buffers directly from the new CPU deltas.
 TArray<const FMorphTargetLODModel*> Morphs;for(const auto& Morph:Mesh->GetMorphTargets())Morphs.Add(&Morph->GetMorphLODModels()[0]);
 auto& LOD=Mesh->GetResourceForRendering()->LODRenderData[0];
 LOD.MorphTargetVertexInfoBuffers.InitMorphResourcesStreaming(GMaxRHIShaderPlatform,LOD.RenderSections,Morphs,Count,.001f);
 Mesh->InitResources();FlushRenderingCommands();
 Body->SetSkeletalMesh(Mesh,false);Body->MarkRenderStateDirty();
 Test.AddInfo(TEXT("UNSAFE DIAGNOSTIC ONLY: transient mesh uses source/procedural corrective before attenuation; not a validated runtime asset."));
 return true;
}


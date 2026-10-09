#include "VamGluteStructureBuilder.h"
#include "VamGluteCorrectiveBuilder.h"
#include "VamGluteCorrectiveAudit.h"
#include "VamGluteCorrectiveGeometry.h"
#include "VamBreastJiggleBuilder.h"
#include "VamLegJiggleProfile.h"
#include "VamBreastJiggleProfile.h"
#include "VamBreastContactProfile.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/MorphTarget.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/SkinWeightVertexBuffer.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/PackageName.h"

namespace
{
template<class T> T* CopyFinal(T* Source,const FString& Path)
{
    if(!Source||FPackageName::DoesPackageExist(Path)||FindPackage(nullptr,*Path))return nullptr;
    auto* R=DuplicateObject<T>(Source,CreatePackage(*Path),*FPackageName::GetLongPackageAssetName(Path));R->SetFlags(RF_Public|RF_Standalone);FAssetRegistryModule::AssetCreated(R);R->MarkPackageDirty();return R;
}
// Render buffers, rather than an earlier MeshDescription, define the bake operator.
bool ActualWeights(USkeletalMesh* M,FVamNativeMeshInput& I,const TArray<int32>& Map,FString& Error)
{
    const auto& L=M->GetResourceForRendering()->LODRenderData[0];const auto& W=L.SkinWeightVertexBuffer;
    if(Map.Num()!=int32(W.GetNumVertices())||!W.GetDataVertexBuffer()->GetWeightData()){Error=TEXT("Final render weights/map unavailable");return false;}
    TArray<TMap<int32,uint16>> Rows;Rows.SetNum(I.Vertices.Num());TSet<int32> Seen;
    for(const auto& S:L.RenderSections)for(uint32 R=S.BaseVertexIndex;R<S.BaseVertexIndex+S.NumVertices;++R){const int V=Map[R];if(!Rows.IsValidIndex(V)){Error=TEXT("Invalid render/input map");return false;}TMap<int32,uint16> Row;for(uint32 K=0;K<W.GetMaxBoneInfluences();++K)if(W.GetBoneWeight(R,K))Row.Add(S.BoneMap[W.GetBoneIndex(R,K)],W.GetBoneWeight(R,K));if(Seen.Contains(V)){if(!Rows[V].OrderIndependentCompareEqual(Row)){Error=TEXT("Render aliases have different actual influences");return false;}}else{Seen.Add(V);Rows[V]=MoveTemp(Row);}}
    I.bInfluencesAreFinalRenderWeights=true;
    const auto Old=I.Influences;I.Influences.Reset();for(const auto& F:Old)if(!Seen.Contains(F.Vertex))I.Influences.Add(F);
    for(int V:Seen){TArray<int32> Keys;Rows[V].GetKeys(Keys);Keys.Sort();for(int B:Keys){FVamBuildInfluence F;F.Vertex=V;F.Bone=B;F.Weight=Rows[V][B]/65535.f;I.Influences.Add(F);}}return true;
}
FString ReplayFinal(UVamCharacterDefinition* D,UVamGluteStructureProfile* G,UVamGluteCorrectiveProfile* P)
{
    using namespace VamGluteGeometry;
    FString Error;FVamNativeMeshInput I;auto* M=D->Body.LoadSynchronous();auto* Shape=D->Shape.LoadSynchronous();auto* Geometry=Shape?Shape->Geometry.LoadSynchronous():nullptr;
    if(!Geometry||!UVamBreastJiggleBuilder::ExtractNative(M,I,Error)||!ActualWeights(M,I,Geometry->RenderToInput,Error))return TEXT("Final replay extraction: ")+Error;
    TArray<FTransform> Bind;for(const auto& B:I.Bones)Bind.Add(B.Parent<0?B.LocalBind:B.LocalBind*Bind[B.Parent]);
    const auto& L=M->GetResourceForRendering()->LODRenderData[0];const auto& W=L.SkinWeightVertexBuffer;auto* Index=L.MultiSizeIndexContainer.GetIndexBuffer();TArray<TSharedPtr<FJsonValue>> Results;
    if(P->AuthoredReplay.Num()!=2*(P->Targets.Num()-1))return TEXT("Authored target data incomplete");
    for(const auto& R:P->AuthoredReplay)
    {
        if(R.PosedDelta.Num()!=I.Vertices.Num())return TEXT("Authored target topology mismatch");
        const FVector Degrees=P->Targets[R.Target].Degrees;const auto CS=Pose(I,*G,Bind,R.Side==0?Degrees:FVector::ZeroVector,R.Side==1?Degrees:FVector::ZeroVector,true);
        TArray<FVector> Native;Native.SetNumZeroed(W.GetNumVertices());
        for(const auto& Basis:P->Bases)if(Basis.Side==R.Side&&Basis.Target==R.Target){auto* Morph=M->FindMorphTarget(Basis.Morph);if(!Morph||Morph->GetMorphLODModels().IsEmpty())return TEXT("Missing baked corrective morph");for(const auto& V:Morph->GetMorphLODModels()[0].Vertices)Native[V.SourceIdx]+=FVector(V.PositionDelta);}
        TArray<FVector> Base,Delta;Base.SetNumZeroed(W.GetNumVertices());Delta=Base;double Maximum=0;int Worst=0,Unsafe=0;
        for(const auto& S:L.RenderSections)for(uint32 V=S.BaseVertexIndex;V<S.BaseVertexIndex+S.NumVertices;++V){FMatrix Skin(ForceInitToZero);for(uint32 K=0;K<W.GetMaxBoneInfluences();++K)if(W.GetBoneWeight(V,K)){const int B=S.BoneMap[W.GetBoneIndex(V,K)];Skin+=Bind[B].ToInverseMatrixWithScale()*CS[B].ToMatrixWithScale()*(W.GetBoneWeight(V,K)/65535.);}
            Base[V]=FVector(Skin.TransformPosition(FVector(L.StaticVertexBuffers.PositionVertexBuffer.VertexPosition(V))));Delta[V]=FVector(Skin.TransformVector(Native[V]));const double E=(Delta[V]-R.PosedDelta[Geometry->RenderToInput[V]]).Size();if(E>Maximum){Maximum=E;Worst=V;}}
        for(int T=0;T<Index->Num();T+=3){const int A=Index->Get(T),B=Index->Get(T+1),C=Index->Get(T+2);if(!TriangleSafe(Base[A],Base[B],Base[C],Delta[A],Delta[B],Delta[C]))++Unsafe;}
        auto O=MakeShared<FJsonObject>();O->SetNumberField(TEXT("side"),R.Side);O->SetStringField(TEXT("target"),P->Targets[R.Target].Name.ToString());O->SetNumberField(TEXT("maximum_delta_error_cm"),Maximum);O->SetNumberField(TEXT("worst_render_vertex"),Worst);O->SetNumberField(TEXT("unsafe_triangles"),Unsafe);Results.Add(MakeShared<FJsonValueObject>(O));
        if(Maximum>.001||Unsafe>0)return FString::Printf(TEXT("Final actual-weight authored replay failed side %d target %d max %.9g cm unsafe %d render %d input %d precision16 %d"),R.Side,R.Target,Maximum,Unsafe,Worst,Geometry->RenderToInput[Worst],W.Use16BitBoneWeight());
    }
    auto Report=MakeShared<FJsonObject>();Report->SetStringField(TEXT("operator"),TEXT("Final LOD0 render buffer quantized actual weights and baked morph deltas; authored posed targets, not neutral bind"));Report->SetArrayField(TEXT("poses"),Results);P->FinalReplayJson=VamGluteAudit::Json(Report);return FString();
}
}
UVamCharacterDefinition* UVamGluteStructureBuilder::BuildCorrective(const FString& Root,UVamCharacterDefinition* D,UVamCharacterDefinition* Original,UVamGluteStructureProfile* G,UVamGluteCorrectiveProfile* P,UVamBreastContactProfile* Contact,const FString& FamilyJson,FString& Error)
{
    Error.Reset();auto Fail=[&](const FString& Why)->UVamCharacterDefinition*{Error=Why;return nullptr;};
    if(!D||!Original||!G||!P||!Contact||D==Original)return Fail(TEXT("Final corrective requires retained, distinct original influence asset"));
    auto* Shape=D->Shape.LoadSynchronous();auto* Geometry=Shape?Shape->Geometry.LoadSynchronous():nullptr;FVamNativeMeshInput I,Source;
    if(!Geometry||!UVamBreastJiggleBuilder::ExtractNative(D->Body.LoadSynchronous(),I,Error)||!UVamBreastJiggleBuilder::ExtractNative(Original->Body.LoadSynchronous(),Source,Error))return nullptr;
    if(Source.Bones.Num()!=G->SourceBoneCount)return Fail(TEXT("Original influence asset must precede Glute/Leg helper insertion"));
    if(!ActualWeights(D->Body.LoadSynchronous(),I,Geometry->RenderToInput,Error))return nullptr;
    I.Morphs.RemoveAll([](const auto& M){return M.Name.ToString().StartsWith(TEXT("G06_"));});
    TArray<TMap<int32,double>> OriginalRows;OriginalRows.SetNum(Source.Vertices.Num());for(const auto& W:Source.Influences)OriginalRows[W.Vertex].Add(W.Bone,W.Weight);
    if(Source.Vertices.Num()>I.Vertices.Num())return Fail(TEXT("Original influence topology exceeds final mesh"));
    for(int V=Source.Vertices.Num();V<I.Vertices.Num();++V){if(!Geometry->RenderRefinementParents.IsValidIndex(V))return Fail(TEXT("Missing deterministic refinement ancestry for original influences"));const auto E=Geometry->RenderRefinementParents[V];if(E.X<0||E.Y<0||E.X>=V||E.Y>=V)return Fail(TEXT("Invalid refinement ancestry"));TMap<int32,double> Row;for(const auto& W:OriginalRows[E.X])Row.FindOrAdd(W.Key)+=W.Value*.5;for(const auto& W:OriginalRows[E.Y])Row.FindOrAdd(W.Key)+=W.Value*.5;OriginalRows.Add(MoveTemp(Row));}
    TArray<FVamBuildInfluence> OriginalWeights;for(int V=0;V<OriginalRows.Num();++V)for(const auto& W:OriginalRows[V]){FVamBuildInfluence F;F.Vertex=V;F.Bone=W.Key;F.Weight=W.Value;OriginalWeights.Add(F);}
    // Generated refinement vertices have no source id: never weld every -1 together.
    auto Ids=Geometry->InputToSource;for(int V=0;V<Ids.Num();++V)if(Ids[V]<0)Ids[V]=-2-V;
    P->OriginalInfluenceDefinition=Original;P->SourceFamilyJson=FamilyJson;
    if(!VamGluteCorrectiveBuilder::Build(I,OriginalWeights,Ids,*G,*P,FamilyJson,Error))return nullptr;
    auto* Mesh=UVamNativeBuilder::BuildMesh(Root+TEXT("/SK_Body"),I,Error);if(!Mesh)return nullptr;
    if(!UVamNativeBuilder::ShareCompatibleSkeleton(Mesh,D->Body.LoadSynchronous()))return Fail(TEXT("Final corrective changed skeleton"));
    const auto Map=UVamNativeBuilder::GetRenderToInputMap(Mesh);const auto OldMap=Geometry->RenderToInput;
    if(OldMap.Num()!=Contact->SurfaceParents.Num())return Fail(TEXT("Original contact render correspondence missing"));
    TArray<int32> FirstOld,FirstNew;FirstOld.Init(INDEX_NONE,I.Vertices.Num());FirstNew=FirstOld;
    for(int R=0;R<OldMap.Num();++R)if(FirstOld[OldMap[R]]<0)FirstOld[OldMap[R]]=R;
    for(int R=0;R<Map.Num();++R)if(FirstNew[Map[R]]<0)FirstNew[Map[R]]=R;
    // UE may choose a different representative for a welded seam after morph insertion.
    // Match source identity / refinement ancestry, never nearest spatial neighbors.
    TArray<FString> Keys;Keys.SetNum(I.Vertices.Num());TMap<FString,int32> OldByKey;
    for(int V=0;V<Keys.Num();++V){const int SourceId=Geometry->InputToSource[V];if(SourceId>=0)Keys[V]=FString::Printf(TEXT("s%d"),SourceId);else{const auto E=Geometry->RenderRefinementParents[V];if(E.X<0||E.Y<0||E.X>=V||E.Y>=V)return Fail(TEXT("Invalid render remap ancestry"));Keys[V]=Keys[E.X]<Keys[E.Y]?TEXT("(")+Keys[E.X]+TEXT(",")+Keys[E.Y]+TEXT(")"):TEXT("(")+Keys[E.Y]+TEXT(",")+Keys[E.X]+TEXT(")");}if(FirstOld[V]>=0&&!OldByKey.Contains(Keys[V]))OldByKey.Add(Keys[V],FirstOld[V]);}
    TSet<int32> Referenced;const auto* RenderIndices=Mesh->GetResourceForRendering()->LODRenderData[0].MultiSizeIndexContainer.GetIndexBuffer();for(int T=0;T<RenderIndices->Num();++T)Referenced.Add(RenderIndices->Get(T));
    const auto Parents=Contact->SurfaceParents;const auto Weights=Contact->SurfaceWeights;const auto Offsets=Contact->SurfaceOffsets;const auto Mask=Contact->SurfaceMask;const auto Nipple=Contact->SurfaceNippleSupport;
    Contact->SurfaceParents.Reset();Contact->SurfaceWeights.Reset();Contact->SurfaceOffsets.Reset();Contact->SurfaceMask.Reset();Contact->SurfaceNippleSupport.Reset();
    for(int NewR=0;NewR<Map.Num();++NewR){const int V=Map[NewR];int R=FirstOld[V];if(R<0){if(const auto* Alias=OldByKey.Find(Keys[V])){R=*Alias;if(!I.Vertices[V].Equals(I.Vertices[OldMap[R]],1.e-5))return Fail(TEXT("Source-equivalent seam positions disagree"));}}
        if(R<0){if(Referenced.Contains(NewR))return Fail(FString::Printf(TEXT("Referenced new render vertex %d input %d source %d has no deterministic binding"),NewR,V,Geometry->InputToSource[V]));Contact->SurfaceParents.Add(FIntVector4(0,0,0,0));Contact->SurfaceWeights.Add(FVector4f(1,0,0,0));Contact->SurfaceOffsets.Add(FVector3f::ZeroVector);Contact->SurfaceMask.Add(0);Contact->SurfaceNippleSupport.Add(0);continue;}
        Contact->SurfaceParents.Add(Parents[R]);Contact->SurfaceWeights.Add(Weights[R]);Contact->SurfaceOffsets.Add(Offsets[R]);Contact->SurfaceMask.Add(Mask[R]);Contact->SurfaceNippleSupport.Add(Nipple[R]);}
    for(auto& T:Contact->MeasurementTriangles){T.X=FirstNew[OldMap[T.X]];T.Y=FirstNew[OldMap[T.Y]];T.Z=FirstNew[OldMap[T.Z]];if(T.X<0||T.Y<0||T.Z<0)return Fail(TEXT("Contact triangle lost during render remap"));}
    auto* Result=CopyFinal(D,Root+TEXT("/CD_Character"));auto* NewShape=CopyFinal(Shape,Root+TEXT("/SD_Shape"));auto* NewGeometry=CopyFinal(Geometry,Root+TEXT("/GD_Bindings"));if(!Result||!NewShape||!NewGeometry)return Fail(TEXT("Final corrective immutable destination conflict"));Result->Body=Mesh;Result->Shape=NewShape;NewShape->Geometry=NewGeometry;NewGeometry->RenderToInput=Map;
    Contact->Body=Mesh;Contact->MarkPackageDirty();
    Error=Contact->ValidateData();if(!Error.IsEmpty())return nullptr;
    Error=ReplayFinal(Result,G,P);P->MarkPackageDirty();return Error.IsEmpty()?Result:nullptr;
}
FString UVamGluteStructureBuilder::SealCorrective(UVamCharacterDefinition* D,UVamGluteStructureProfile* G,UVamLegJiggleProfile* L,UVamBreastJiggleProfile* B,UVamGluteCorrectiveProfile* P)
{
    if(!P||!D)return TEXT("Missing corrective seal input");FString Error=ReplayFinal(D,G,P);if(!Error.IsEmpty())return Error;P->FinalDataDigests=P->ComputeFinalData(D,G,L,B,Error);P->MarkPackageDirty();return Error;
}
FString UVamGluteStructureBuilder::ValidateCorrective(UVamCharacterDefinition* D,UVamGluteStructureProfile* G,UVamLegJiggleProfile* L,UVamBreastJiggleProfile* B,UVamGluteCorrectiveProfile* P)
{
    if(!P)return TEXT("Missing corrective");const FString Error=P->ValidateFinalData(D,G,L,B);return Error.IsEmpty()?ReplayFinal(D,G,P):Error;
}

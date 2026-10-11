#include "VamGluteStructureBuilder.h"
#include "VamGluteCorrectiveBuilder.h"
#include "VamGluteCorrectiveAudit.h"
#include "VamGluteCorrectiveDetail.h"
#include "VamGluteSkinningResidual.h"
#include "VamGluteCorrectiveGeometry.h"
#include "VamBreastJiggleBuilder.h"
#include "VamLegJiggleProfile.h"
#include "VamBreastJiggleProfile.h"
#include "VamBreastContactProfile.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/MorphTarget.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Rendering/SkeletalMeshLODModel.h"
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
bool PrepareFinal(UVamCharacterDefinition* D,UVamCharacterDefinition* Original,UVamGluteStructureProfile* G,FVamNativeMeshInput& I,TArray<FVamBuildInfluence>& OriginalWeights,TArray<int32>& Ids,FString& Error)
{
    auto Fail=[&](const FString& Why){Error=Why;return false;};
    auto* Shape=D->Shape.LoadSynchronous();auto* Geometry=Shape?Shape->Geometry.LoadSynchronous():nullptr;FVamNativeMeshInput Source;
    if(!Geometry||!UVamBreastJiggleBuilder::ExtractNative(D->Body.LoadSynchronous(),I,Error)||!UVamBreastJiggleBuilder::ExtractNative(Original->Body.LoadSynchronous(),Source,Error))return false;
    if(Source.Bones.Num()!=G->SourceBoneCount)return Fail(TEXT("Original influence asset must precede Glute/Leg helper insertion"));
    if(!ActualWeights(D->Body.LoadSynchronous(),I,Geometry->RenderToInput,Error))return false;
    I.Morphs.RemoveAll([](const auto& M){return M.Name.ToString().StartsWith(TEXT("G06_"));});
    TArray<TMap<int32,double>> OriginalRows;OriginalRows.SetNum(Source.Vertices.Num());for(const auto& W:Source.Influences)OriginalRows[W.Vertex].Add(W.Bone,W.Weight);
    if(Source.Vertices.Num()>I.Vertices.Num())return Fail(TEXT("Original influence topology exceeds final mesh"));
    for(int V=Source.Vertices.Num();V<I.Vertices.Num();++V){if(!Geometry->RenderRefinementParents.IsValidIndex(V))return Fail(TEXT("Missing deterministic refinement ancestry for original influences"));const auto E=Geometry->RenderRefinementParents[V];if(E.X<0||E.Y<0||E.X>=V||E.Y>=V)return Fail(TEXT("Invalid refinement ancestry"));TMap<int32,double> Row;for(const auto& W:OriginalRows[E.X])Row.FindOrAdd(W.Key)+=W.Value*.5;for(const auto& W:OriginalRows[E.Y])Row.FindOrAdd(W.Key)+=W.Value*.5;OriginalRows.Add(MoveTemp(Row));}
    OriginalWeights.Reset();for(int V=0;V<OriginalRows.Num();++V)for(const auto& W:OriginalRows[V]){FVamBuildInfluence F;F.Vertex=V;F.Bone=W.Key;F.Weight=W.Value;OriginalWeights.Add(F);}
    // Generated refinement vertices have no source id: never weld every -1 together.
    Ids=Geometry->InputToSource;for(int V=0;V<Ids.Num();++V)if(Ids[V]<0)Ids[V]=-2-V;
    return true;
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
    auto* Shape=D->Shape.LoadSynchronous();auto* Geometry=Shape?Shape->Geometry.LoadSynchronous():nullptr;FVamNativeMeshInput I;TArray<FVamBuildInfluence> OriginalWeights;TArray<int32> Ids;
    if(!PrepareFinal(D,Original,G,I,OriginalWeights,Ids,Error))return nullptr;
    P->OriginalInfluenceDefinition=Original;P->SourceFamilyJson=FamilyJson;
    if(!VamGluteCorrectiveBuilder::Build(I,OriginalWeights,Ids,*G,*P,FamilyJson,Error))return nullptr;
    auto* Mesh=UVamNativeBuilder::BuildMesh(Root+TEXT("/SK_Body"),I,Error);if(!Mesh)return nullptr;
    if(!UVamNativeBuilder::ShareCompatibleSkeleton(Mesh,D->Body.LoadSynchronous()))return Fail(TEXT("Final corrective changed skeleton"));
    // Skinning normals alone omit spatial derivatives of the weight field, and
    // summing independently baked morph normal deltas is not the final surface
    // normal. Use UE Skin Cache's post-deformation reconstruction for sections
    // actually influenced by Glute/Leg helpers. No project-wide forced override.
    auto& LOD=Mesh->GetImportedModel()->LODModels[0];int32 RecomputeSections=0;
    for(const auto& Section:LOD.Sections)
    {
        bool Affected=false;
        for(const auto& V:Section.SoftVertices)for(int32 K=0;K<MAX_TOTAL_INFLUENCES;++K)
            if(V.InfluenceWeights[K] && Section.BoneMap[V.InfluenceBones[K]]>=G->SourceBoneCount)Affected=true;
        if(!Affected)continue;
        auto& Settings=LOD.UserSectionsData.FindOrAdd(Section.OriginalDataSectionIndex);
        Settings.bRecomputeTangent=true;Settings.RecomputeTangentsVertexMaskChannel=ESkinVertexColorChannel::None;
        ++RecomputeSections;
    }
    if(!RecomputeSections)return Fail(TEXT("No lower-body section for final surface normals"));
    Mesh->GetLODInfo(0)->SkinCacheUsage=ESkinCacheUsage::Enabled;
    Mesh->PostEditChange();
    UE_LOG(LogTemp,Display,TEXT("LOWER_SURFACE_NORMALS native Skin Cache sections=%d"),RecomputeSections);
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

FString UVamGluteStructureBuilder::ExportCorrectiveDiagnostics(UVamCharacterDefinition* D,UVamGluteStructureProfile* G,UVamLegJiggleProfile* L,UVamBreastJiggleProfile* B,UVamGluteCorrectiveProfile* P,const FString& Directory)
{
    if(!D||!P||!G||Directory.IsEmpty())return TEXT("Missing B2 diagnostic input");
    FString Error=P->ValidateFinalData(D,G,L,B);if(!Error.IsEmpty())return Error;
    FVamNativeMeshInput I;TArray<FVamBuildInfluence> OriginalWeights;TArray<int32> Ids;
    if(!PrepareFinal(D,P->OriginalInfluenceDefinition.LoadSynchronous(),G,I,OriginalWeights,Ids,Error))return Error;
    auto* Scratch=DuplicateObject<UVamGluteCorrectiveProfile>(P,GetTransientPackage());FVamGluteCorrectiveDetail Detail(Directory);
    if(!VamGluteCorrectiveBuilder::Build(I,OriginalWeights,Ids,*G,*Scratch,P->SourceFamilyJson,Error,&Detail))return Error;
    double ReplayError=0;if(Scratch->AuthoredReplay.Num()!=P->AuthoredReplay.Num())return TEXT("B2 target count differs from baked asset");
    for(int T=0;T<P->AuthoredReplay.Num();++T){const auto& A=P->AuthoredReplay[T];const auto& C=Scratch->AuthoredReplay[T];if(A.PosedDelta.Num()!=C.PosedDelta.Num())return TEXT("B2 topology differs");for(int V=0;V<A.PosedDelta.Num();++V)ReplayError=FMath::Max(ReplayError,(A.PosedDelta[V]-C.PosedDelta[V]).Size());}
    if(ReplayError>.001)return FString::Printf(TEXT("B2 decomposition no longer matches sealed bake: %.9g cm"),ReplayError);
    Detail.Meta->SetNumberField(TEXT("rebake_max_error_cm"),ReplayError);Detail.Meta->SetStringField(TEXT("definition"),D->GetPathName());Detail.Meta->SetStringField(TEXT("profile"),P->GetPathName());Detail.Meta->SetStringField(TEXT("provenance"),P->Provenance);
    Detail.Meta->SetStringField(TEXT("scope"),TEXT("Reused G062 bake, swing-twist/RBF runtime weights. Raw source is verified Bend Fix mapping / thigh-X reference, NOT independently measured VaM shapes for every target. 130-degree combinations below are safety probes only. Safety scalars are exact accumulated multiplicative factors; before fields are after masking/limits/smoothing and before safety."));
    auto Seal=MakeShared<FJsonObject>();for(const auto& K:P->FinalDataDigests)Seal->SetStringField(K.Key,K.Value);Detail.Meta->SetObjectField(TEXT("final_data_seal"),Seal);
    Detail.Field(TEXT("rest_positions"),I.Vertices,TEXT("input_bind_position_cm"));TArray<TSharedPtr<FJsonValue>> Targets,Bases,Probes,Sides;
    for(int T=0;T<P->Targets.Num();++T){auto O=MakeShared<FJsonObject>();O->SetNumberField(TEXT("index"),T);O->SetStringField(TEXT("name"),P->Targets[T].Name.ToString());O->SetStringField(TEXT("kind"),TEXT("authored_interpolation_target_not_independent_VaM_capture"));O->SetArrayField(TEXT("degrees"),{MakeShared<FJsonValueNumber>(P->Targets[T].Degrees.X),MakeShared<FJsonValueNumber>(P->Targets[T].Degrees.Y),MakeShared<FJsonValueNumber>(P->Targets[T].Degrees.Z)});Targets.Add(MakeShared<FJsonValueObject>(O));}
    for(const auto& Basis:P->Bases){auto O=MakeShared<FJsonObject>();O->SetStringField(TEXT("morph"),Basis.Morph.ToString());O->SetNumberField(TEXT("side"),Basis.Side);O->SetNumberField(TEXT("target"),Basis.Target);O->SetNumberField(TEXT("axis"),Basis.Axis);Bases.Add(MakeShared<FJsonValueObject>(O));}
    for(int Step=0;Step<72;++Step){const auto V=VamGluteGeometry::SafetyProbe(*P,Step);auto O=MakeShared<FJsonObject>();O->SetNumberField(TEXT("step"),Step);O->SetStringField(TEXT("kind"),Step<P->Targets.Num()?TEXT("authored_target_also_checked_for_safety"):TEXT("synthetic_safety_probe_NOT_VaM_target"));O->SetArrayField(TEXT("degrees"),{MakeShared<FJsonValueNumber>(V.X),MakeShared<FJsonValueNumber>(V.Y),MakeShared<FJsonValueNumber>(V.Z)});Probes.Add(MakeShared<FJsonValueObject>(O));}
    for(int S=0;S<2;++S){const auto& Side=G->Sides[S];TArray<double> Region,Front,Lateral,Transition;const FVector D=Side.Dimensions,Thigh=Side.RestThighInAnchor.GetLocation();
        for(int V=0;V<I.Vertices.Num();++V){Region.Add(Side.RegionWeights[V]);auto Gaussian=[&](const FVector& Center,const FVector& Width){const FVector Q=(Side.RegionPoints[V]-Center)/(D*Width);return FMath::Exp(-Q.SizeSquared()*2);};
            Front.Add(Gaussian(Thigh+FVector(-D.X*.28,0,-D.Z*.05),FVector(.55,.4,.45)));Lateral.Add(Gaussian(Side.Regions[4].Rest,FVector(.6,.4,.6)));Transition.Add(Gaussian(Side.FoldSemanticMap.MiddleTransition,FVector(.55,.55,.3)));}
        const FString Prefix=FString::Printf(TEXT("s%d_"),S);Detail.Scalar(Prefix+TEXT("region_mask"),Region);Detail.Scalar(Prefix+TEXT("roi_anterior_hip"),Front);Detail.Scalar(Prefix+TEXT("roi_lateral_glute"),Lateral);Detail.Scalar(Prefix+TEXT("roi_glute_thigh"),Transition);
    }
    Detail.Meta->SetStringField(TEXT("roi_scope"),TEXT("Engineering inspection ROIs in posterior/transverse/superior anchor coordinates, not anatomical ground truth; independent of corrective region mask so missing coverage remains visible. anterior: thigh head + (-.28*AP,0,-.05*SI), widths (.55,.4,.45); lateral: Lateral region, widths (.6,.4,.6); transition: MiddleTransition, widths (.55,.55,.3); gaussian exp(-2*distance_squared)."));
    Detail.Meta->SetArrayField(TEXT("targets"),Targets);Detail.Meta->SetArrayField(TEXT("bases"),Bases);Detail.Meta->SetArrayField(TEXT("safety_probes"),Probes);return Detail.Save()?FString():TEXT("Could not save B2 diagnostic fields");
}

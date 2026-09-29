#include "VamGluteSurfaceSnapshot.h"
#include "VamGluteSkeletalMeshComponent.h"
#include "VamGluteCorrectiveAudit.h"
#include "VamNativeBuilder.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/MorphTarget.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Rendering/SkeletalMeshLODModel.h"
#include "BoneContainer.h"
#include "DrawDebugHelpers.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

FString CaptureGluteSurface(UVamGluteSkeletalMeshComponent& B,bool Draw,bool Jiggle)
{
    auto* Mesh=B.GetSkeletalMeshAsset();auto* P=B.CorrectiveProfile.Get();auto* G=B.GluteProfile.Get();
    if(!Mesh || !P || !G || B.GluteRest.Num()!=2 || B.HipPoseState.Sides.Num()!=2 || !Mesh->GetImportedModel() || Mesh->GetImportedModel()->LODModels.IsEmpty()) return TEXT("No initialized Glute instance / native LOD0.");
    if(B.GetPredictedLODLevel()!=0) return TEXT("Snapshot supports LOD0. Move closer and capture again.");
    const auto& LOD=Mesh->GetImportedModel()->LODModels[0];const auto Map=UVamNativeBuilder::GetRenderToInputMap(Mesh);const auto& Pose=B.GetComponentSpaceTransforms();
    if(Map.Num()!=int32(LOD.NumVertices) || Pose.Num()!=Mesh->GetRefSkeleton().GetNum()) return TEXT("Pose / LOD correspondence unavailable.");
    const auto Override=B.GetRefPoseOverride();const auto& Inverse=Override.IsValid()?Override->RefBasesInvMatrix:Mesh->GetRefBasesInvMatrix();
    TArray<FMatrix> BoneMatrices;for(int32 N=0;N<Pose.Num();++N) BoneMatrices.Add(FMatrix(Inverse[N])*Pose[N].ToMatrixWithScale());
    auto StructuralMatrices=BoneMatrices;
    if(Jiggle)
    {
        if(!B.GluteJiggleProfile || B.GluteStates.Num()!=2) return TEXT("G1 profile / current rest absent.");
        for(int32 Side=0;Side<2;++Side) for(int32 N=0;N<5;++N)
        {
            const int32 Bone=B.GluteRest[Side].Regions[N].BoneIndex;
            const FTransform Rest=B.GluteStates[Side].Regions[N].Transform*B.GluteRest[Side].AnchorLocal*B.HipPoseState.PelvisComponent;
            StructuralMatrices[Bone]=FMatrix(Inverse[Bone])*Rest.ToMatrixWithScale();
        }
    }
    TArray<FVector> Base,Correction,Actual;Base.SetNumZeroed(LOD.NumVertices);Correction=Base;Actual=Base;
    TArray<TArray<double>> TargetWeights;for(const auto& H:B.HipPoseState.Sides) TargetWeights.Add(VamGluteCorrective::Weights(*P,H));
    for(const auto& Section:LOD.Sections) for(int32 J=0;J<Section.SoftVertices.Num();++J) Base[Section.BaseVertexIndex+J]=FVector(Section.SoftVertices[J].Position);
    auto Report=MakeShared<FJsonObject>();auto Curves=MakeShared<FJsonObject>();
    for(const UMorphTarget* Morph:Mesh->GetMorphTargets())
    {
        const float Applied=B.AppliedCorrectiveWeight(Morph->GetFName());const auto* Basis=P->Bases.FindByPredicate([&](const auto& X){return X.Morph==Morph->GetFName();});
        double Weight=Applied;if(Basis) Weight=TargetWeights[Basis->Side][Basis->Target]*VamGluteCorrective::ShapeScale(P->BuildDimensions[Basis->Side],B.GluteRest[Basis->Side].Dimensions)[Basis->Axis];
        if(FMath::Abs(Weight)<1.e-10 && FMath::Abs(Applied)<1.e-10) continue;Curves->SetNumberField(Morph->GetName(),Applied);
        if(Morph->GetMorphLODModels().IsEmpty()) continue;
        for(const auto& D:Morph->GetMorphLODModels()[0].Vertices) if(Base.IsValidIndex(D.SourceIdx)) { if(Basis) { Correction[D.SourceIdx]+=FVector(D.PositionDelta)*Weight;Actual[D.SourceIdx]+=FVector(D.PositionDelta)*Applied; }else Base[D.SourceIdx]+=FVector(D.PositionDelta)*Applied; }
    }
    TArray<FVector> Off=Base,Delta=Base,AppliedDelta=Base;
    for(const auto& Section:LOD.Sections) for(int32 J=0;J<Section.SoftVertices.Num();++J)
    {
        const auto& X=Section.SoftVertices[J];const int32 V=Section.BaseVertexIndex+J;FVector A=FVector::ZeroVector,D=A,C=A;
        for(int32 K=0;K<MAX_TOTAL_INFLUENCES;++K) if(X.InfluenceWeights[K]) { const double W=double(X.InfluenceWeights[K])/65535.;const auto& M=BoneMatrices[Section.BoneMap[X.InfluenceBones[K]]];A+=FVector(M.TransformPosition(Base[V]))*W;D+=FVector(M.TransformVector(Correction[V]))*W;C+=FVector(M.TransformVector(Actual[V]))*W; }
        if(Jiggle)
        {
            const FVector Vertex=Base[V]+Actual[V];A=D=FVector::ZeroVector;
            for(int32 K=0;K<MAX_TOTAL_INFLUENCES;++K) if(X.InfluenceWeights[K]) { const int32 Bone=Section.BoneMap[X.InfluenceBones[K]];const double W=double(X.InfluenceWeights[K])/65535.;const FVector Rest=FVector(StructuralMatrices[Bone].TransformPosition(Vertex));A+=Rest*W;D+=(FVector(BoneMatrices[Bone].TransformPosition(Vertex))-Rest)*W; }
            C=D;
        }
        Off[V]=A;Delta[V]=D;AppliedDelta[V]=C;
    }
    Report->SetStringField(TEXT("scope"),TEXT("Current instance native LOD0, final pose and active Shape/Morph buffers. Corrective OFF vs hypothetical ON with identical pose; excludes material WPO, cloth and GPU readback."));
    if(Jiggle) { Report->SetStringField(TEXT("scope"),TEXT("G1 OFF structural rest vs current G1 dynamics; SAME current G0.6.2 Morph weights. Native LOD0 CPU reconstruction, excludes WPO/cloth/GPU readback."));Report->SetStringField(TEXT("jiggle_profile"),B.GluteJiggleProfile->GetPathName()); }
    if(Jiggle)
    {
        Report->SetBoolField(TEXT("g1_enabled"),B.bGluteJiggleEnabled);Report->SetStringField(TEXT("g1_diagnostics"),B.GluteJiggleDiagnostics());
        Report->SetNumberField(TEXT("support_scale"),B.GluteSupport);Report->SetNumberField(TEXT("damping_scale"),B.GluteDamping);Report->SetNumberField(TEXT("mobility_scale"),B.GluteMobility);Report->SetNumberField(TEXT("coupling_scale"),B.GluteInternalCoupling);Report->SetNumberField(TEXT("mass_scale"),B.GluteMassScale);
        Report->SetStringField(TEXT("primary_pelvis"),B.HipPoseState.PelvisComponent.ToHumanReadableString());Report->SetStringField(TEXT("primary_left_femur"),B.HipPoseState.LeftFemurComponent.ToHumanReadableString());Report->SetStringField(TEXT("primary_right_femur"),B.HipPoseState.RightFemurComponent.ToHumanReadableString());
    }
    Report->SetStringField(TEXT("mesh"),Mesh->GetPathName());Report->SetStringField(TEXT("profile"),P->GetPathName());Report->SetNumberField(TEXT("shape_revision"),B.GluteShapeRevision);Report->SetBoolField(TEXT("corrective_enabled"),B.bCorrectiveEnabled);Report->SetBoolField(TEXT("structural_enabled"),B.bGluteEnabled);Report->SetObjectField(TEXT("applied_morph_weights"),Curves);
    TArray<int32> Ids;for(int32 V=0;V<Map.Num();++V) Ids.Add(Map[V]);FString Summary=TEXT("当前姿态快照（同姿态 OFF 青色 / ON 品红；1:1，显示 15 秒）\n");
    if(Jiggle) Summary=TEXT("G1 当前姿态表面：OFF 青色 / 当前动态 品红；G0.6 Morph 保持一致\n");
    TSet<int32> Used;for(uint32 V:LOD.IndexBuffer) Used.Add(V);
    for(int32 Side=0;Side<2;++Side)
    {
        auto S=G->Sides[Side];S.RegionWeights.SetNum(Map.Num());S.RegionPoints.SetNum(Map.Num());
        for(int32 V=0;V<Map.Num();++V) { S.RegionWeights[V]=Used.Contains(V)?G->Sides[Side].RegionWeights[Map[V]]:0;S.RegionPoints[V]=G->Sides[Side].RegionPoints[Map[V]]; }
        const auto Stats=VamGluteAudit::Regions(Delta,S,Ids);Report->SetObjectField(S.Side.ToString(),Stats);Report->SetObjectField(S.Side.ToString()+TEXT("_applied"),VamGluteAudit::Regions(AppliedDelta,S,Ids));
        const auto Whole=Stats->GetObjectField(TEXT("whole"));Summary+=FString::Printf(TEXT("%s RMS %.3f / P95 %.3f / Max %.3f cm\n"),*S.Side.ToString(),Whole->GetNumberField(TEXT("rms")),Whole->GetNumberField(TEXT("p95")),Whole->GetNumberField(TEXT("max")));
    }
    if(Draw && B.GetWorld()) for(int32 T=0;T<LOD.IndexBuffer.Num();T+=3)
    {
        const int32 A=LOD.IndexBuffer[T],C=LOD.IndexBuffer[T+1],D=LOD.IndexBuffer[T+2];bool InRegion=false;
        for(int32 V:{A,C,D}) for(int32 Side=0;Side<2;++Side) if(G->Sides[Side].RegionWeights[Map[V]]>.01) InRegion=true;
        if(!InRegion) continue;
        for(int32 K=0;K<3;++K) { const int32 U=LOD.IndexBuffer[T+K],V=LOD.IndexBuffer[T+(K+1)%3];DrawDebugLine(B.GetWorld(),B.GetComponentTransform().TransformPosition(Off[U]),B.GetComponentTransform().TransformPosition(Off[V]),FColor::Cyan,false,15,0,.35f);DrawDebugLine(B.GetWorld(),B.GetComponentTransform().TransformPosition(Off[U]+Delta[U]),B.GetComponentTransform().TransformPosition(Off[V]+Delta[V]),FColor::Magenta,false,15,0,.35f); }
    }
    const FString Directory=FPaths::ProjectSavedDir()/TEXT("VamDiagnostics");IFileManager::Get().MakeDirectory(*Directory,true);const FString Path=Directory/(TEXT("GluteSurface_")+FGuid::NewGuid().ToString(EGuidFormats::Digits)+TEXT(".json"));
    if(!FFileHelper::SaveStringToFile(VamGluteAudit::Json(Report),*Path)) return Summary+TEXT("保存失败");
    return Summary+TEXT("快照已保存：")+Path;
}

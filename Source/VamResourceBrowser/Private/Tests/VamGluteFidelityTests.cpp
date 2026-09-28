#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "VamRuntimeConfiguration.h"
#include "VamCharacterDefinition.h"
#include "VamBreastJiggleBuilder.h"
#include "VamGluteCorrectiveProfile.h"
#include "VamGluteCorrectiveAudit.h"
#include "VamGluteCorrectiveGeometry.h"
#include "VamGluteRotationBlend.h"
#include "VamShapeData.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/MorphTarget.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Rendering/SkeletalMeshLODModel.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVamGluteSurfaceTest,"Vam.Glute.SurfaceTruth",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FVamGluteSurfaceTest::RunTest(const FString& Parameters)
{
    using namespace VamGluteGeometry;FString Path;FParse::Value(FCommandLine::Get(),TEXT("VamGluteTestConfig="),Path);
    auto* C=LoadObject<UVamRuntimeConfiguration>(nullptr,*Path);if(!TestNotNull(TEXT("Runtime config"),C)) return false;
    auto* P=C->GluteCorrective.LoadSynchronous();auto* G=C->GluteStructure.LoadSynchronous();auto* Def=C->Definition.LoadSynchronous();auto* Body=Def->Body.LoadSynchronous();
    if(!TestNotNull(TEXT("Fidelity profile"),P) || !TestTrue(TEXT("Fidelity schema"),P->SchemaVersion>=2)) return false;
    FVamNativeMeshInput Mesh;FString Error;if(!TestTrue(TEXT("Native input"),UVamBreastJiggleBuilder::ExtractNative(Body,Mesh,Error))) return false;
    const auto* Geometry=Def->Shape.LoadSynchronous()->Geometry.LoadSynchronous();const auto Map=UVamNativeBuilder::GetRenderToInputMap(Body);
    // Reconstruct the actual LOD domain. UE can weld distinct retained input
    // vertices; filling an unmapped input vertex with zero invents discontinuities.
    const auto InputMorphs=Mesh.Morphs;const auto& LOD=Body->GetImportedModel()->LODModels[0];
    Mesh.Vertices.SetNum(LOD.NumVertices);Mesh.Influences.Reset();Mesh.Triangles.Reset();
    for(uint32 V:LOD.IndexBuffer) Mesh.Triangles.Add(V);
    TArray<int32> Ids;Ids.SetNum(LOD.NumVertices);
    if(!TestEqual(TEXT("LOD correspondence domain"),Map.Num(),int32(LOD.NumVertices))) return false;
    for(const auto& Section:LOD.Sections) for(int32 J=0;J<Section.SoftVertices.Num();++J)
    {
        const int32 V=Section.BaseVertexIndex+J;const auto& X=Section.SoftVertices[J];Mesh.Vertices[V]=FVector(X.Position);Ids[V]=Geometry->InputToSource[Map[V]];
        for(int32 K=0;K<MAX_TOTAL_INFLUENCES;++K) if(X.InfluenceWeights[K]) { FVamBuildInfluence F;F.Vertex=V;F.Bone=Section.BoneMap[X.InfluenceBones[K]];F.Weight=double(X.InfluenceWeights[K])/65535.;Mesh.Influences.Add(F); }
    }
    TArray<FTransform> Bind;for(const auto& B:Mesh.Bones) Bind.Add(B.Parent<0?B.LocalBind:B.LocalBind*Bind[B.Parent]);
    TMap<FName,FDeltaField> RenderMorphs,ExpectedMorphs;
    for(const auto& Basis:P->Bases)
    {
        const auto* M=Body->FindMorphTarget(Basis.Morph);if(!TestNotNull(TEXT("Native render Morph exists"),M)) return false;
        auto& F=RenderMorphs.Add(Basis.Morph);F.SetNumZeroed(Mesh.Vertices.Num());
        if(!M->GetMorphLODModels().IsEmpty()) for(const auto& D:M->GetMorphLODModels()[0].Vertices) { if(!TestTrue(TEXT("Render source index mapped"),Map.IsValidIndex(D.SourceIdx))) return false;F[D.SourceIdx]=FVector(D.PositionDelta); }
        const auto* Retained=InputMorphs.FindByPredicate([&](const auto& X){return X.Name==Basis.Morph;});bool Consistent=true;
        for(int32 V=0;V<Map.Num();++V) Consistent&=F[V].Equals(Retained->Deltas[Map[V]],1.e-4);TestTrue(TEXT("Native render Morph agrees with retained geometry within quantization"),Consistent);auto& Expected=ExpectedMorphs.Add(Basis.Morph);for(int32 V:Map) Expected.Add(Retained->Deltas[V]);
    }
    TArray<TSharedPtr<FJsonValue>> Results;double FlexRms[2][3]={{0}};
    for(int32 Side=0;Side<4;++Side)
    {
        const int32 RegionSide=Side%2;auto S=G->Sides[RegionSide];S.RegionWeights.SetNum(Map.Num());S.RegionPoints.SetNum(Map.Num());
        for(int32 V=0;V<Map.Num();++V) { S.RegionWeights[V]=G->Sides[RegionSide].RegionWeights[Map[V]];S.RegionPoints[V]=G->Sides[RegionSide].RegionPoints[Map[V]]; }
        TSet<int32> Used;for(int32 V:Mesh.Triangles) Used.Add(V);for(int32 V=0;V<Ids.Num();++V) if(!Used.Contains(V)) S.RegionWeights[V]=0;
        for(int32 Step=0;Step<72;++Step)
        {
            FVector D;if(Step<P->Targets.Num()) D=P->Targets[Step].Degrees;else if(Step<18) D=FVector((Step-10)*12,20*FMath::Sin(double(Step)),20*FMath::Cos(double(Step)));else { const int32 K=(Step-18)%36;D=FVector(Step>=54?(Step<63?-40.:130.):(K/9)*30.,(K/3)%3==0?-20.:(K/3)%3==1?0.:35.,K%3==0?-25.:K%3==1?0.:30.); }
            FVamHipSidePose H;H.FlexionExtension=D.X*PI/180.;H.AbductionAdduction=D.Y*PI/180.;H.ExternalInternalRotation=D.Z*PI/180.;const auto W=VamGluteCorrective::Weights(*P,H);
            const auto Skin=VamGluteGeometry::Skin(Mesh,Mesh.Influences,Bind,Pose(Mesh,*G,Bind,Side!=1?D:FVector::ZeroVector,Side!=0?D:FVector::ZeroVector,true));const auto Base=Apply(Skin,Mesh.Vertices,true);FDeltaField Delta,Expected;Delta.SetNumZeroed(Ids.Num());Expected=Delta;
            for(const auto& B:P->Bases) if(Side>=2 || B.Side==Side) { const auto& F=RenderMorphs[B.Morph];for(int32 V=0;V<Ids.Num();++V) { Delta[V]+=F[V]*W[B.Target];Expected[V]+=ExpectedMorphs[B.Morph][V]*W[B.Target]; } }
            // NativeBuilder validates each rendered basis to 1e-4 cm per
            // component (UE's minimum omission threshold). Test exact retained
            // seam identity separately, then propagate measured basis error.
            bool Seams=true,Edges=true;double SeamMax=0,QuantizationMax=0;TMap<int32,int32> Seen;
            for(int32 V:Used) { QuantizationMax=FMath::Max(QuantizationMax,(Expected[V]-Delta[V]).Size());if(const auto* Previous=Seen.Find(Ids[V])) { const int32 U=*Previous;const double Gap=(Delta[U]-Delta[V]).Size(),Bound=(Delta[U]-Expected[U]).Size()+(Delta[V]-Expected[V]).Size()+1.e-6;SeamMax=FMath::Max(SeamMax,Gap);Seams&=Expected[U].Equals(Expected[V],1.e-6) && Gap<=Bound; }else Seen.Add(Ids[V],V); }
            for(int32 T=0;T<Mesh.Triangles.Num();T+=3) for(int32 K=0;K<3;++K) { const int32 A=Mesh.Triangles[T+K],B=Mesh.Triangles[T+(K+1)%3];Edges&=(Delta[A]-Delta[B]).Size()<FMath::Max(.15,(Mesh.Vertices[A]-Mesh.Vertices[B]).Size()*3); }
            TestTrue(TEXT("Retained seam identity and measured render omission bound"),Seams);TestTrue(TEXT("Complete corrective no local edge spikes"),Edges);
            const auto Field=Apply(Skin,Delta);auto Stats=VamGluteAudit::Regions(Field,S,Ids);auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("side"),S.Side.ToString());Row->SetNumberField(TEXT("step"),Step);Row->SetBoolField(TEXT("bilateral"),Side>=2);Row->SetNumberField(TEXT("maximum_seam_gap_cm"),SeamMax);Row->SetNumberField(TEXT("maximum_render_omission_error_cm"),QuantizationMax);Row->SetObjectField(TEXT("surface"),Stats);Results.Add(MakeShared<FJsonValueObject>(Row));
            if(Step<4)
            {
                auto Residual=RotationBlend(Mesh,Bind,Pose(Mesh,*G,Bind,Side!=1?D:FVector::ZeroVector,Side!=0?D:FVector::ZeroVector,true));
                for(int32 V=0;V<Ids.Num();++V) Residual[V]-=Base[V];
                Row->SetObjectField(TEXT("rotation_blend_minus_lbs"),VamGluteAudit::Regions(Residual,S,Ids));
                if(Step==0) { double Maximum=0;for(const auto& V:Residual) Maximum=FMath::Max(Maximum,V.Size());TestTrue(TEXT("Neutral rotation blend equals LBS"),Maximum<1.e-5); }
            }
            bool Safe=true,Finite=true,Matrix=true;for(int32 V:Used) { Finite&=!Field[V].ContainsNaN();if(!Delta[V].IsNearlyZero(1.e-8)) Matrix&=FMath::Abs(Skin[V].Determinant())>=.01; }
            for(int32 T=0;T<Mesh.Triangles.Num();T+=3) { const int32 A=Mesh.Triangles[T],B=Mesh.Triangles[T+1],C0=Mesh.Triangles[T+2];const bool Good=TriangleSafe(Base[A],Base[B],Base[C0],Field[A],Field[B],Field[C0]);if(!Good && Safe) { const FVector N0=FVector::CrossProduct(Base[B]-Base[A],Base[C0]-Base[A]),N1=FVector::CrossProduct(Base[B]+Field[B]-Base[A]-Field[A],Base[C0]+Field[C0]-Base[A]-Field[A]);AddInfo(FString::Printf(TEXT("SURFACE_FAILURE side %d step %d tri %d area0 %.12g area1 %.12g dot %.12g ratio %.12g"),Side,Step,T/3,N0.Size(),N1.Size(),FVector::DotProduct(N0,N1),N1.Size()/FMath::Max(1.e-20,N0.Size()))); }Safe&=Good; }
            TestTrue(FString::Printf(TEXT("No inversion/explosion, side %d pose %d"),Side,Step),Safe);TestTrue(TEXT("Surface finite"),Finite);TestTrue(TEXT("Affected matrices nonsingular"),Matrix);
            if(Step==0) TestEqual(TEXT("Neutral rendered displacement zero"),Stats->GetObjectField(TEXT("whole"))->GetNumberField(TEXT("max")),0.);
            if(Side<2 && Step>=1 && Step<=3) FlexRms[Side][Step-1]=Stats->GetObjectField(TEXT("whole"))->GetNumberField(TEXT("rms"));
        }
        if(Side<2) TestTrue(TEXT("Flex30 < Flex60 < Flex90 surface RMS"),FlexRms[Side][0]<FlexRms[Side][1] && FlexRms[Side][1]<FlexRms[Side][2]);
    }
    TSharedPtr<FJsonObject> Audit;TestTrue(TEXT("Immutable audit parses"),FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(P->FidelityAuditJson),Audit));
    if(Audit) for(const auto& Value:Audit->GetArrayField(TEXT("targets")))
    {
        const auto E=Value->AsObject();const auto St=E->GetObjectField(TEXT("stages"));const auto Before=St->GetObjectField(TEXT("05_procedural_masked"))->GetObjectField(TEXT("whole"));const auto After=St->GetObjectField(TEXT("06_procedural_smoothed"))->GetObjectField(TEXT("whole"));
        TestTrue(TEXT("Diffusion preserves surface RMS"),FMath::Abs(Before->GetNumberField(TEXT("rms"))-After->GetNumberField(TEXT("rms")))<1.e-6);
        const auto& A=Before->GetArrayField(TEXT("mean"));const auto& B=After->GetArrayField(TEXT("mean"));for(int32 K=0;K<3;++K) TestTrue(TEXT("Diffusion preserves weighted vector mean"),FMath::Abs(A[K]->AsNumber()-B[K]->AsNumber())<1.e-6);
        if(P->ReusedSourceVertexCount>0 && E->GetStringField(TEXT("target"))==TEXT("Flex90"))
        {
            const double Reference=St->GetObjectField(TEXT("reference_source_posed"))->GetObjectField(TEXT("whole"))->GetNumberField(TEXT("rms"));const double Final=St->GetObjectField(TEXT("08_source_after_target_safety"))->GetObjectField(TEXT("whole"))->GetNumberField(TEXT("rms"));
            TestTrue(TEXT("Verified source retains at least 80 percent surface RMS"),Final/Reference>=.8 && Final/Reference<=1.1);
        }
    }
    if(P->ReusedSourceVertexCount==0)
    {
        TSharedPtr<FJsonObject> Reference;TestTrue(TEXT("Fallback immutable family reference"),FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(P->FamilyReferenceJson),Reference));
        if(Reference) for(int32 Side=0;Side<2;++Side)
        {
            const auto Ref=Reference->GetArrayField(TEXT("sides"))[Side]->AsObject();const auto& S=G->Sides[Side];auto Vec=[](const auto& V){return FVector(V[0]->AsNumber(),V[1]->AsNumber(),V[2]->AsNumber());};const FVector Dim=Vec(Ref->GetArrayField(TEXT("dimensions")));
            const double Length=FMath::Sqrt(FMath::Pow(S.EffectiveVolumeCm3/Ref->GetNumberField(TEXT("volume")),1./3.)*FMath::Sqrt(S.SupportAreaCm2/Ref->GetNumberField(TEXT("support_area"))))/FMath::Pow((S.Dimensions/Dim).X*(S.Dimensions/Dim).Y*(S.Dimensions/Dim).Z,1./3.);
            for(const auto& V:Ref->GetArrayField(TEXT("targets"))) if(V->AsObject()->GetStringField(TEXT("target"))==TEXT("Flex90"))
            {
                const FVector Expected=Vec(V->AsObject()->GetObjectField(TEXT("whole"))->GetArrayField(TEXT("axis_rms")))/Dim*S.Dimensions*Length;const auto* Diagnostic=P->Diagnostics.FindByPredicate([&](const auto& X){return X.Side==Side && X.Target==3;});const double Ratio=Diagnostic->ProceduralRms/Expected.Size();
                TestTrue(FString::Printf(TEXT("Fallback family-normalized surface RMS same order, side %d ratio %.5f"),Side,Ratio),Ratio>=.5 && Ratio<=2.);
            }
        }
    }
    auto Out=MakeShared<FJsonObject>();Out->SetArrayField(TEXT("render_surface_truth"),Results);Out->SetStringField(TEXT("configuration"),Path);FString Output;if(FParse::Value(FCommandLine::Get(),TEXT("VamGluteSurfaceReport="),Output)) TestTrue(TEXT("Surface evidence saved"),FFileHelper::SaveStringToFile(VamGluteAudit::Json(Out),*Output));
    return true;
}
#endif

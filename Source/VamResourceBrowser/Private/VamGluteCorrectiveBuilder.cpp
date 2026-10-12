#include "VamGluteCorrectiveBuilder.h"
#include "VamGluteCorrectiveAudit.h"
#include "VamGluteCorrectiveGeometry.h"
#include "VamGluteSkinningResidual.h"
#include "VamGluteCorrectiveDetail.h"
#include "VamMorphNormalUtils.h"
#include "Misc/SecureHash.h"
#include <cmath>

namespace
{
using namespace VamGluteGeometry;
FVector Vec(const TArray<TSharedPtr<FJsonValue>>& A) { return FVector(A[0]->AsNumber(),A[1]->AsNumber(),A[2]->AsNumber()); }
TArray<TSharedPtr<FJsonValue>> JsonVec(FVector V) { return {MakeShared<FJsonValueNumber>(V.X),MakeShared<FJsonValueNumber>(V.Y),MakeShared<FJsonValueNumber>(V.Z)}; }
FDeltaField Local(const FDeltaField& F,const FTransform& A) { auto R=F;for(auto& V:R) V=A.InverseTransformVectorNoScale(V);return R; }
// Explicit DAZSkinV2 CPU isolated thigh-X port. Source node order and disjoint
// graft node domains are retained. This excludes source post-skin smoothing.
TArray<FMatrix> SourceReference(const FVamNativeMeshInput& I,const TArray<int32>& Ids,const TArray<FTransform>& Bind,const FJsonObject& Source,const FVector& Degrees,const FString& OnlyBone=FString())
{
    TArray<FMatrix> M;M.Init(FMatrix::Identity,I.Vertices.Num());TMap<int32,TArray<int32>> Map;for(int32 V=0;V<Ids.Num();++V) Map.FindOrAdd(Ids[V]).Add(V);
    const double Angle=-Degrees.X*PI/180.,Bulge=Source.GetNumberField(TEXT("source_bulge_scale"));
    for(const auto& Value:Source.GetArrayField(TEXT("source_skin_nodes")))
    {
        const auto N=Value->AsObject();const FString Name=N->GetStringField(TEXT("name"));const int32 Bone=I.Bones.IndexOfByPredicate([&](const auto& B){return B.Name.ToString()==Name;});check(Bone>=0);
        if(!OnlyBone.IsEmpty() && Name!=OnlyBone) continue;
        const auto Factors=N->GetObjectField(TEXT("bulgeFactors"));
        auto Add=[&](int32 Id,double Weight,double Left,double Right,bool Full)
        {
            if(const auto* Vertices=Map.Find(Id))
            {
                const double L=FMath::Abs(Angle)>.01 && !Full?1+Factors->GetNumberField(Angle<0?TEXT("xnegleft"):TEXT("xposleft"))*Angle*Bulge*Left:1;
                const double R=FMath::Abs(Angle)>.01 && !Full?1+Factors->GetNumberField(Angle<0?TEXT("xnegright"):TEXT("xposright"))*Angle*Bulge*Right:1;
                const FTransform T(FQuat(FVector::YAxisVector,Angle*Weight),FVector::ZeroVector,FVector(L*R,1,L*R));
                const FMatrix A=Bind[Bone].ToInverseMatrixWithScale()*T.ToMatrixWithScale()*Bind[Bone].ToMatrixWithScale();for(int32 V:*Vertices) M[V]=M[V]*A;
            }
        };
        for(const auto& Row:N->GetArrayField(TEXT("weights"))) { const auto W=Row->AsObject();const bool Full=W->GetNumberField(TEXT("xweight"))>.99999 && W->GetNumberField(TEXT("yweight"))>.99999 && W->GetNumberField(TEXT("zweight"))>.99999;Add(W->GetIntegerField(TEXT("vertex")),Full?1:W->GetNumberField(TEXT("xweight")),W->GetNumberField(TEXT("xleftbulge")),W->GetNumberField(TEXT("xrightbulge")),Full); }
        for(const auto& V:N->GetArrayField(TEXT("fullyWeightedVertices"))) Add(int32(V->AsNumber()),1,0,0,true);
    }return M;
}
void AddMorph(FVamNativeMeshInput& I,UVamGluteCorrectiveProfile& P,const FVamGluteSide& S,int32 Side,int32 Target,const FDeltaField& Native,const TArray<FMatrix>& Skin,const FTransform& Anchor)
{
    const FDeltaField Field=Local(Apply(Skin,Native),Anchor);
    for(int32 Axis=0;Axis<3;++Axis)
    {
        FVamBuildMorph M;M.Name=FName(*FString::Printf(TEXT("G06_%s_%s_%d"),*S.Side.ToString(),*P.Targets[Target].Name.ToString(),Axis));M.Deltas.SetNumZeroed(I.Vertices.Num());
        FVamGluteCorrectiveBasis B;B.Morph=M.Name;B.Side=Side;B.Target=Target;B.Axis=Axis;B.RegionalRmsCm.Init(0,5);TArray<double> RW;RW.Init(0,5);
        for(int32 V=0;V<Field.Num();++V)
        {
            FVector D=FVector::ZeroVector;D[Axis]=Field[V][Axis];if(!D.IsNearlyZero(1.e-12)) M.Deltas[V]=FVector(Skin[V].Inverse().TransformVector(Anchor.TransformVectorNoScale(D)));
            B.MaximumCm=FMath::Max(B.MaximumCm,FMath::Abs(D[Axis]));
            for(int32 N=0;N<5;++N) { const double W=FMath::Exp(-((S.RegionPoints[V]-S.Regions[N].Rest)/S.Dimensions).SizeSquared()*12)*S.RegionWeights[V];B.RegionalRmsCm[N]+=D.SizeSquared()*W;RW[N]+=W; }
        }
        if(B.MaximumCm<1.e-6) continue;
        for(int32 N=0;N<5;++N) B.RegionalRmsCm[N]=FMath::Sqrt(B.RegionalRmsCm[N]/FMath::Max(.001,RW[N]));
        M.NormalDeltas=VamMorphNormals::Deltas(I.Vertices,I.Normals,I.Triangles,M.Deltas);P.Bases.Add(MoveTemp(B));I.Morphs.Add(MoveTemp(M));
    }
}
}

bool VamGluteCorrectiveBuilder::Build(FVamNativeMeshInput& I,const TArray<FVamBuildInfluence>& OriginalInfluences,const TArray<int32>& Ids,const UVamGluteStructureProfile& G,UVamGluteCorrectiveProfile& P,const FString& FamilyJson,FString& Error,FVamGluteCorrectiveDetail* Detail)
{
    using namespace VamGluteGeometry;auto Fail=[&](const FString& Why){Error=Why;return false;};TSharedPtr<FJsonObject> Family;
    if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(FamilyJson),Family)) return Fail(TEXT("Invalid corrective family JSON"));
    const TSharedPtr<FJsonObject>* Policy=nullptr;if(!Family->TryGetObjectField(TEXT("glute_corrective"),Policy)) return Fail(TEXT("Unsupported family: corrective policy missing"));
    P.SchemaVersion=4;P.Algorithm=TEXT("glute-corrective-final-weights-v1");P.SkeletonFamily=G.SkeletonFamily;P.SourceTopologyIdentity=G.SourceTopologyIdentity;P.Targets.Reset();P.Bases.Reset();P.BuildDimensions.Reset();P.Diagnostics.Reset();P.ReusedSourceVertexCount=0;P.AuthoredReplay.Reset();P.FinalDataDigests.Reset();
    P.FamilyPolicyIdentity=FMD5::HashAnsiString(*VamGluteAudit::Json(*Policy));P.MetricDegrees=Vec((*Policy)->GetArrayField(TEXT("metric_degrees")));P.BlendWidth=(*Policy)->GetNumberField(TEXT("blend_width"));P.MaximumDimensionFraction=(*Policy)->GetNumberField(TEXT("maximum_dimension_fraction"));
    for(const auto& V:(*Policy)->GetArrayField(TEXT("targets"))) { const auto O=V->AsObject();FVamGlutePoseTarget T;T.Name=FName(*O->GetStringField(TEXT("name")));T.Degrees=Vec(O->GetArrayField(TEXT("degrees")));P.Targets.Add(T); }
    const TSharedPtr<FJsonObject>* Source=nullptr;Family->TryGetObjectField(TEXT("glute_corrective_source"),Source);const bool Verified=Source && !(*Source)->GetArrayField(TEXT("deltas")).IsEmpty();
    if(Verified && (*Source)->GetIntegerField(TEXT("version"))!=2) return Fail(TEXT("Corrective requires verified merged topology mapping v2"));
    bool ContinuousSafety=false;(*Policy)->TryGetBoolField(TEXT("continuous_safety"),ContinuousSafety);
    double ContinuityEdgeBudget=.25;(*Policy)->TryGetNumberField(TEXT("continuity_edge_budget"),ContinuityEdgeBudget);
    if(!FMath::IsFinite(ContinuityEdgeBudget) || ContinuityEdgeBudget<=0 || ContinuityEdgeBudget>.5) { Error=TEXT("continuity_edge_budget must be in (0, 0.5]");return false; }
    bool SourceFieldOperator=false;(*Policy)->TryGetBoolField(TEXT("source_operator_in_source_field"),SourceFieldOperator);
    if(SourceFieldOperator && (!Verified || !(*Source)->HasField(TEXT("bulge_preparation")))) return Fail(TEXT("Source field operator requires initialized source bulge weights"));
    const TSharedPtr<FJsonObject>* Calibration=nullptr;(*Policy)->TryGetObjectField(TEXT("family_reference"),Calibration);
    if(!Verified && !Calibration) return Fail(TEXT("No source: family corrective reference calibration required"));
    P.Provenance=Verified?TEXT("Verified !Bend Fix; Boundary graft transfer; source-only TriAx thigh-X reference; independent-side extension; target-local safety. Procedural residual separately gated/smoothed."):TEXT("Procedural family moment calibration; no source vertex deltas reused. ");
    if(Source) P.Provenance+=(*Source)->GetStringField(TEXT("reason"));
    if(Ids.Num()!=I.Vertices.Num()) return Fail(TEXT("Corrective topology mapping mismatch"));
    auto Audit=MakeShared<FJsonObject>();Audit->SetStringField(TEXT("algorithm"),P.Algorithm);Audit->SetStringField(TEXT("reference_scope"),TEXT("CPU source thigh-X equations and verified average-angle driver; excludes VaM post-skin smoothing/physics. Source and native LBS baselines are separately measured."));
    if(Verified) Audit->SetObjectField(TEXT("source_mapping"),(*Source)->GetObjectField(TEXT("topology_mapping")));
    TMap<int32,FVector> RawMap,SourceMap;FDeltaField SourcePositions;
    if(Verified)
    {
        for(const auto& V:(*Source)->GetArrayField(TEXT("raw_deltas"))) { const auto& D=V->AsArray();RawMap.Add(int32(D[0]->AsNumber()),FVector(D[1]->AsNumber(),D[2]->AsNumber(),D[3]->AsNumber())); }
        for(const auto& V:(*Source)->GetArrayField(TEXT("deltas"))) { const auto& D=V->AsArray();SourceMap.Add(int32(D[0]->AsNumber()),FVector(D[1]->AsNumber(),D[2]->AsNumber(),D[3]->AsNumber())); }
        for(const auto& V:(*Source)->GetArrayField(TEXT("neutral_vertices_cm"))) SourcePositions.Add(Vec(V->AsArray()));
    }
    { FDeltaField Raw;TArray<double> Mask;TArray<int32> Keys;for(const auto& V:RawMap) { Raw.Add(V.Value);Mask.Add(1);Keys.Add(V.Key); }Audit->SetObjectField(TEXT("raw_source_unit_all"),VamGluteAudit::Distribution(Raw,Mask,Keys)); }
    TArray<FTransform> Bind;for(const auto& B:I.Bones) Bind.Add(B.Parent<0?B.LocalBind:B.LocalBind*Bind[B.Parent]);
    TArray<TArray<int32>> Adj;Adj.SetNum(I.Vertices.Num());TMap<int32,TArray<int32>> Aliases;TSet<int32> Used;
    for(int32 V=0;V<Ids.Num();++V) Aliases.FindOrAdd(Ids[V]).Add(V);
    for(int32 T=0;T<I.Triangles.Num();T+=3) for(int32 K=0;K<3;++K) { const int32 A=I.Triangles[T+K],B=I.Triangles[T+(K+1)%3];Adj[A].AddUnique(B);Adj[B].AddUnique(A);Used.Add(A);Used.Add(B); }
    for(const auto& Pair:Aliases) for(int32 A:Pair.Value) for(int32 B:Pair.Value) if(A!=B) Adj[A].AddUnique(B);
    // Fields remain indexed by retained source topology, but all amplitude
    // calibration/statistics below use triangle-referenced surface vertices.
    TArray<FVamGluteSide> Surface=G.Sides;for(auto& S:Surface) for(int32 V=0;V<Ids.Num();++V) if(!Used.Contains(V)) S.RegionWeights[V]=0;
    TArray<FVector> SourceScales;SourceScales.Init(FVector::OneVector,2);TArray<TSharedPtr<FJsonValue>> Entries,ReferenceSides;TArray<TArray<FDeltaField>> SourceNative,ProcNative;SourceNative.SetNum(2);ProcNative.SetNum(2);
    FTargetFields UnattenuatedSource,UnattenuatedProc;UnattenuatedSource.SetNum(2);UnattenuatedProc.SetNum(2);
    TArray<TArray<TArray<FMatrix>>> TargetSkin;TargetSkin.SetNum(2);TArray<TArray<TSharedPtr<FJsonObject>>> Stage;Stage.SetNum(2);
    TArray<TArray<FDeltaField>> RawFields,AdaptFields,ReferenceFields;RawFields.SetNum(2);AdaptFields.SetNum(2);ReferenceFields.SetNum(2);
    for(int32 Side=0;Side<2;++Side)
    {
        const auto& S=G.Sides[Side];const auto& SS=Surface[Side];const FTransform Anchor=S.AnchorLocal*Bind[S.PelvisBone];P.BuildDimensions.Add(S.Dimensions);
        auto RefSide=MakeShared<FJsonObject>();RefSide->SetArrayField(TEXT("dimensions"),JsonVec(S.Dimensions));RefSide->SetNumberField(TEXT("volume"),S.EffectiveVolumeCm3);RefSide->SetNumberField(TEXT("support_area"),S.SupportAreaCm2);TArray<TSharedPtr<FJsonValue>> RefTargets;
        if(Detail)Detail->Begin(Side,0,P.Targets.Num());
        UnattenuatedSource[Side].SetNum(P.Targets.Num());UnattenuatedProc[Side].SetNum(P.Targets.Num());SourceNative[Side].SetNum(P.Targets.Num());ProcNative[Side].SetNum(P.Targets.Num());TargetSkin[Side].SetNum(P.Targets.Num());Stage[Side].SetNum(P.Targets.Num());RawFields[Side].SetNum(P.Targets.Num());AdaptFields[Side].SetNum(P.Targets.Num());ReferenceFields[Side].SetNum(P.Targets.Num());
        FVector Mean0=FVector::ZeroVector,Mean1=FVector::ZeroVector,V0=FVector::ZeroVector,V1=FVector::ZeroVector;double Total=0;
        for(int32 V=0;V<Ids.Num();++V) if(SourcePositions.IsValidIndex(Ids[V])) { const double W=SS.RegionWeights[V];Mean0+=Anchor.InverseTransformPosition(SourcePositions[Ids[V]])*W;Mean1+=S.RegionPoints[V]*W;Total+=W; }
        if(Total>0) { Mean0/=Total;Mean1/=Total; }for(int32 V=0;V<Ids.Num();++V) if(SourcePositions.IsValidIndex(Ids[V])) { const FVector A=Anchor.InverseTransformPosition(SourcePositions[Ids[V]])-Mean0,B=S.RegionPoints[V]-Mean1;V0+=A*A*SS.RegionWeights[V];V1+=B*B*SS.RegionWeights[V]; }
        FVector Scale=FVector::OneVector;for(int32 A=0;A<3;++A) if(V0[A]>.001) Scale[A]=FMath::Clamp(FMath::Sqrt(V1[A]/V0[A]),.25,4.);
        SourceScales[Side]=Scale;double MaxRegion=0;for(int32 V=0;V<Ids.Num();++V) MaxRegion=FMath::Max(MaxRegion,double(G.Sides[0].RegionWeights[V]+G.Sides[1].RegionWeights[V]));
        for(int32 Target=1;Target<P.Targets.Num();++Target)
        {
            const FVector Degrees=P.Targets[Target].Degrees;auto& St=Stage[Side][Target];St=MakeShared<FJsonObject>();auto Capture=[&](const TCHAR* Name,const FDeltaField& F){St->SetObjectField(Name,VamGluteAudit::Regions(F,SS,Ids));};
            const auto Posed=Pose(I,G,Bind,Side==0?Degrees:FVector::ZeroVector,Side==1?Degrees:FVector::ZeroVector,true);auto& Skin=TargetSkin[Side][Target];Skin=VamGluteGeometry::Skin(I,I.Influences,Bind,Posed);
            auto Thigh=S.RestThighInAnchor;Thigh.SetRotation(Rotation(S,Degrees)*Thigh.GetRotation());const auto Structure=VamGluteStructure::Evaluate(G,S,Thigh);
            FDeltaField Raw,Adapt,SourceField,Proc;Raw.SetNumZeroed(Ids.Num());Adapt=Raw;SourceField=Raw;Proc=Raw;TArray<double> Boundary;Boundary.Init(0,Ids.Num());
            const double Driver=FMath::Clamp(Degrees.X/100.,0.,1.);
            auto RefSkin=Verified?SourceReference(I,Ids,Bind,**Source,FVector(Degrees.X,0,0)):Skin;
            FDeltaField SourceOperatorDelta;
            if(SourceFieldOperator && FMath::Abs(Degrees.X)>1.e-8)
            {
                const FVector Flex(Degrees.X,0,0);
                const auto FlexPose=Pose(I,G,Bind,Side==0?Flex:FVector::ZeroVector,Side==1?Flex:FVector::ZeroVector,true);
                const auto FlexDQ=RotationBlend(I,Bind,FlexPose),FullDQ=RotationBlend(I,Bind,Posed),Base=Apply(Skin,I.Vertices,true);
                const auto SourceFlex=Apply(SourceReference(I,Ids,Bind,**Source,Flex,I.Bones[S.ThighBone].Name.ToString()),I.Vertices,true);
                SourceOperatorDelta.SetNum(I.Vertices.Num());
                for(int32 V=0;V<I.Vertices.Num();++V)SourceOperatorDelta[V]=FullDQ[V]+SourceFlex[V]-FlexDQ[V]-Base[V];
                Capture(TEXT("source_operator_posed_before_localization"),Local(SourceOperatorDelta,Anchor));
            }
            for(int32 V=0;V<Ids.Num();++V)
            {
                if(const auto* D=RawMap.Find(Ids[V])) Raw[V]=Anchor.InverseTransformVectorNoScale(*D)*Driver;
                if(const auto* D=SourceMap.Find(Ids[V])) Adapt[V]=Anchor.InverseTransformVectorNoScale(*D)*Scale*Driver;
            }
            if(Detail){Detail->Field(Detail->Key(Side,Target,TEXT("raw_source_local")),Raw,TEXT("anchor_local_delta_cm"));Detail->Field(Detail->Key(Side,Target,TEXT("adapted_source_local")),Adapt,TEXT("anchor_local_delta_cm"));}
            Capture(TEXT("01_raw_source"),Raw);Capture(TEXT("02_mapped_adapted_source"),Adapt);RawFields[Side][Target]=Raw;AdaptFields[Side][Target]=Adapt;
            const auto RefPosed=Apply(RefSkin,[&](){auto F=Adapt;for(auto& V:F) V=Anchor.TransformVectorNoScale(V);return F;}());ReferenceFields[Side][Target]=RefPosed;
            Capture(TEXT("reference_source_posed"),Local(RefPosed,Anchor));
            auto Reference=VamGluteAudit::Regions(Local(RefPosed,Anchor),SS,Ids);Reference->SetStringField(TEXT("target"),P.Targets[Target].Name.ToString());Reference->SetNumberField(TEXT("driver_weight"),Driver);
            TArray<TSharedPtr<FJsonValue>> Attach;for(const auto& R:S.Regions) Attach.Add(MakeShared<FJsonValueNumber>(R.ThighAttachment));Reference->SetArrayField(TEXT("thigh_attachment"),Attach);RefTargets.Add(MakeShared<FJsonValueObject>(Reference));
            for(int32 V=0;V<Ids.Num();++V)
            {
                const double Both=G.Sides[0].RegionWeights[V]+G.Sides[1].RegionWeights[V];const double Localize=Both>1.e-12?S.RegionWeights[V]/Both*FMath::SmoothStep(0.,MaxRegion*.01,Both):0;
                SourceField[V]=Anchor.InverseTransformVectorNoScale(RefPosed[V])*Localize;
                if(!SourceOperatorDelta.IsEmpty())SourceField[V]+=Anchor.InverseTransformVectorNoScale(SourceOperatorDelta[V])*Localize;
                Boundary[V]=FMath::Min(1.,double(S.RegionWeights[V])*8);
                Proc[V]=VamGluteCorrective::CurvatureDelta(S,S.RegionPoints[V],Degrees,Structure.FoldState)*S.RegionWeights[V];
                // Verified response owns its support. Procedural curvature fills
                // remaining geometry rather than modifying the trusted source.
                if(Verified) Proc[V]*=1-FMath::SmoothStep(0.,.01,Adapt[V].Size()/S.Dimensions.GetMin());
            }
            Capture(TEXT("03_side_localized_source"),SourceField);
            if(!Verified)
            {
                const auto Ref=(*Calibration)->GetArrayField(TEXT("sides"))[Side]->AsObject();const auto RefDims=Vec(Ref->GetArrayField(TEXT("dimensions")));const auto& Samples=Ref->GetArrayField(TEXT("targets"));TSharedPtr<FJsonObject> Sample;
                for(const auto& V:Samples) if(V->AsObject()->GetStringField(TEXT("target"))==P.Targets[Target].Name.ToString()) Sample=V->AsObject();
                const bool HasFlex=Degrees.X>0;
                if(!HasFlex) for(const auto& V:Samples) if(V->AsObject()->GetStringField(TEXT("target"))==TEXT("Flex60")) Sample=V->AsObject();
                if(!Sample) return Fail(TEXT("Family reference target absent"));
                const double ShapeLength=FMath::Sqrt(FMath::Pow(S.EffectiveVolumeCm3/Ref->GetNumberField(TEXT("volume")),1./3.)*FMath::Sqrt(S.SupportAreaCm2/Ref->GetNumberField(TEXT("support_area"))))/FMath::Pow((S.Dimensions/RefDims).X*(S.Dimensions/RefDims).Y*(S.Dimensions/RefDims).Z,1./3.);
                const double PoseAmount=HasFlex?1:(Degrees/P.MetricDegrees).Size();
                for(int32 V=0;V<Ids.Num();++V)
                {
                    FVector D=FVector::ZeroVector;double Sum=0;
                    for(int32 N=0;N<5;++N)
                    {
                        const auto Stats=Sample->GetObjectField(S.Regions[N].Semantic.ToString());const FVector Mean=Vec(Stats->GetArrayField(TEXT("mean")))/RefDims,Rms=Vec(Stats->GetArrayField(TEXT("axis_rms")))/RefDims;
                        const FVector Q=(S.RegionPoints[V]-S.Regions[N].Rest)/S.Dimensions;const double K=FMath::Exp(-Q.SizeSquared()*12);FVector Std;for(int32 A=0;A<3;++A) Std[A]=FMath::Sqrt(FMath::Max(0.,Rms[A]*Rms[A]-Mean[A]*Mean[A]));
                        const double Attachment=FMath::Sqrt((1+S.Regions[N].ThighAttachment)/(1+Sample->GetArrayField(TEXT("thigh_attachment"))[N]->AsNumber()));
                        D+=(Mean+Std*FVector(Q.Z*4,Q.Y*4,Q.Z*4))*S.Dimensions*(K*Attachment);Sum+=K;
                    }
                    Proc[V]=Sum>1.e-12?D/Sum*Boundary[V]*ShapeLength*PoseAmount:FVector::ZeroVector;
                }
                // Match source regional axis energy with smooth semantic fields;
                // no vertex deltas or character identity are stored in calibration.
                for(int32 Pass=0;Pass<12;++Pass)
                {
                    FVector Gains[5];for(int32 N=0;N<5;++N)
                    {
                        const auto Stats=Sample->GetObjectField(S.Regions[N].Semantic.ToString());const FVector Desired=Vec(Stats->GetArrayField(TEXT("axis_rms")))/RefDims*S.Dimensions*ShapeLength*PoseAmount;
                        const FVector Actual=Vec(VamGluteAudit::Distribution(Proc,VamGluteAudit::Mask(SS,N),Ids)->GetArrayField(TEXT("axis_rms")));
                        for(int32 A=0;A<3;++A) Gains[N][A]=FMath::Loge(FMath::Max(1.e-8,Desired[A])/FMath::Max(1.e-8,Actual[A]));
                    }
                    for(int32 V=0;V<Ids.Num();++V) { FVector Log=FVector::ZeroVector;double Sum=0;for(int32 N=0;N<5;++N) { const double K=FMath::Exp(-((S.RegionPoints[V]-S.Regions[N].Rest)/S.Dimensions).SizeSquared()*12);Log+=Gains[N]*K;Sum+=K; }if(Sum>1.e-12) for(int32 A=0;A<3;++A) Proc[V][A]*=FMath::Exp(Log[A]/Sum*.5); }
                }
            }
            Capture(TEXT("04_procedural_before_limit"),Proc);
            for(auto& D:Proc) { const double L=D.Size(),Limit=S.Dimensions.GetMin()*P.MaximumDimensionFraction;if(L>1.e-12) D*=Limit*std::tanh(L/Limit)/L; }
            Weld(Proc,Aliases);Capture(TEXT("05_procedural_masked"),Proc);auto SmoothWeights=VamGluteAudit::Mask(SS,-1);TSet<int32> Unique;for(int32 V=0;V<Ids.Num();++V) if(SmoothWeights[V]>1.e-6) { if(Unique.Contains(Ids[V])) SmoothWeights[V]=0;else Unique.Add(Ids[V]); }Smooth(Proc,Adj,Boundary,SmoothWeights,Aliases);Capture(TEXT("06_procedural_smoothed"),Proc);
            auto& NS=SourceNative[Side][Target];auto& NP=ProcNative[Side][Target];NS.SetNumZeroed(Ids.Num());NP=NS;
            for(int32 V=0;V<Ids.Num();++V)
            {
                if(SourceField[V].IsNearlyZero(1.e-12) && Proc[V].IsNearlyZero(1.e-12)) continue;
                if(FMath::Abs(Skin[V].Determinant())<.01) return Fail(TEXT("Corrective target skin matrix singular"));
                const auto Inverse=Skin[V].Inverse();NS[V]=FVector(Inverse.TransformVector(Anchor.TransformVectorNoScale(SourceField[V])));NP[V]=FVector(Inverse.TransformVector(Anchor.TransformVectorNoScale(Proc[V])));
            }
            Weld(NS,Aliases);Weld(NP,Aliases);
            Capture(TEXT("07_source_preskin_before_safety"),NS);
            if(Detail){Detail->Project(Side,Target,TEXT("source_before"),NS,Skin,Anchor);Detail->Project(Side,Target,TEXT("procedural_before"),NP,Skin,Anchor);Detail->SourceSafety[Side][Target].Init(1,Ids.Num());}
            UnattenuatedSource[Side][Target]=NS;UnattenuatedProc[Side][Target]=NP;
            auto Combined=NS;for(int32 V=0;V<Ids.Num();++V) Combined[V]+=NP[V];TArray<double> Safety;
            if(!Condition(I,Skin,Combined,Aliases,Safety)) return Fail(TEXT("Target local safety did not converge"));
            for(int32 V=0;V<Ids.Num();++V) { NS[V]*=Safety[V];NP[V]*=Safety[V];if(Detail)Detail->SourceSafety[Side][Target][V]*=Safety[V]; }
        }
        RefSide->SetArrayField(TEXT("targets"),RefTargets);ReferenceSides.Add(MakeShared<FJsonValueObject>(RefSide));
    }
    // Validate blends in their own pose, and only alter substantially active
    // targets at failing surface vertices. No global per-vertex worst-case mask.
    for(int32 Iteration=0;Iteration<64;++Iteration)
    {
        bool Changed=false;double ContinuityReduction=0;
        if(ContinuousSafety)for(int32 S=0;S<2;++S)for(int32 T=1;T<P.Targets.Num();++T)
        {
            const auto& Original=UnattenuatedSource[S][T];const auto& OriginalProc=UnattenuatedProc[S][T];
            auto& A=SourceNative[S][T];auto& B=ProcNative[S][T];TArray<double> Factors;Factors.Init(1,A.Num());
            for(int32 V=0;V<A.Num();++V)
            {
                const double Den=Original[V].SizeSquared()+OriginalProc[V].SizeSquared();
                if(Den>1.e-20)Factors[V]=FMath::Clamp((FVector::DotProduct(A[V],Original[V])+FVector::DotProduct(B[V],OriginalProc[V]))/Den,0.,1.);
            }
            const auto Before=Factors;ContinuousAttenuation(I,Original,OriginalProc,Aliases,Factors,ContinuityEdgeBudget);
            for(int32 V=0;V<A.Num();++V)if(Factors[V]<Before[V]-1.e-6 && Original[V].SizeSquared()+OriginalProc[V].SizeSquared()>1.e-20)
            {
                ContinuityReduction=FMath::Max(ContinuityReduction,Before[V]-Factors[V]);Changed=true;A[V]=Original[V]*Factors[V];B[V]=OriginalProc[V]*Factors[V];
                if(Detail)Detail->SourceSafety[S][T][V]=Factors[V];
            }
        }
    // Source operators can be much larger than a source Morph increment.
    // Condition every independently exported AP/ML/SI basis before treating
    // these fields as fixed input to the residual stage. Keep the same native
    // edge and posed triangle predicates; do not bypass a failed fixed basis.
    if(SourceFieldOperator) for(int32 S=0;S<2;++S) for(int32 T=1;T<P.Targets.Num();++T)
    {
        const auto& M=TargetSkin[S][T];const FTransform Anchor=G.Sides[S].AnchorLocal*Bind[G.Sides[S].PelvisBone];
        auto& A=SourceNative[S][T];auto& B=ProcNative[S][T];
        for(int32 Pass=0;Pass<64;++Pass)
        {
            TArray<double> Factors;Factors.Init(1,I.Vertices.Num());
            for(int32 Axis=0;Axis<3;++Axis)
            {
                FDeltaField Projected;Projected.SetNumZeroed(I.Vertices.Num());
                for(int32 V=0;V<Projected.Num();++V)
                {
                    if(FMath::Abs(M[V].Determinant())<.01)continue;
                    const FVector Local=Anchor.InverseTransformVectorNoScale(FVector(M[V].TransformVector(A[V]+B[V])));FVector D=FVector::ZeroVector;D[Axis]=Local[Axis];
                    Projected[V]=FVector(M[V].Inverse().TransformVector(Anchor.TransformVectorNoScale(D)));
                }
                TArray<double> Safety;if(!Condition(I,M,Projected,Aliases,Safety))return Fail(TEXT("Source operator axis safety did not converge"));
                for(int32 V=0;V<Factors.Num();++V)Factors[V]=FMath::Min(Factors[V],Safety[V]);
            }
            bool PassChanged=false;for(int32 V=0;V<Factors.Num();++V)if(Factors[V]<.999999)
            {
                PassChanged=true;Changed=true;A[V]*=Factors[V];B[V]*=Factors[V];if(Detail)Detail->SourceSafety[S][T][V]*=Factors[V];
            }
            if(!PassChanged)break;if(Pass==63)return Fail(TEXT("Source operator common axis safety did not converge"));
        }
    }
        for(int32 Side=0;Side<3;++Side) for(int32 Step=0;Step<72;++Step)
        {
            FVector D;
            if(Step<P.Targets.Num()) D=P.Targets[Step].Degrees;
            else if(Step<18) D=FVector((Step-10)*12,20*FMath::Sin(double(Step)),20*FMath::Cos(double(Step)));
            else { const int32 K=(Step-18)%36;D=FVector(Step>=54?(Step<63?-40.:130.):(K/9)*30.,(K/3)%3==0?-20.:(K/3)%3==1?0.:35.,K%3==0?-25.:K%3==1?0.:30.); }
            FVamHipSidePose H;H.FlexionExtension=D.X*PI/180.;H.AbductionAdduction=D.Y*PI/180.;H.ExternalInternalRotation=D.Z*PI/180.;const auto W=VamGluteCorrective::Weights(P,H);
            const auto Skin=VamGluteGeometry::Skin(I,I.Influences,Bind,Pose(I,G,Bind,Side!=1?D:FVector::ZeroVector,Side!=0?D:FVector::ZeroVector,true));FDeltaField Combined;Combined.SetNumZeroed(Ids.Num());
            for(int32 S=0;S<2;++S) if(Side==2 || S==Side) for(int32 T=1;T<W.Num();++T) for(int32 V=0;V<Ids.Num();++V) Combined[V]+=(SourceNative[S][T][V]+ProcNative[S][T][V])*W[T];
            TArray<double> Safety;if(!Condition(I,Skin,Combined,Aliases,Safety)) return Fail(TEXT("Combined pose safety did not converge"));
            for(int32 V=0;V<Ids.Num();++V) if(Safety[V]<.999999)
            {
                // Weight alone is insufficient after local conditioning: a
                // low-weight target may become the only remaining contributor.
                // Select by actual displacement contribution at this vertex.
                double Max=0;for(int32 S=0;S<2;++S) if(Side==2 || S==Side) for(int32 T=1;T<W.Num();++T) Max=FMath::Max(Max,(SourceNative[S][T][V]+ProcNative[S][T][V]).Size()*W[T]);
                if(Max<=1.e-16) continue;Changed=true;
                for(int32 S=0;S<2;++S) if(Side==2 || S==Side) for(int32 T=1;T<W.Num();++T)
                {
                    const double Contribution=(SourceNative[S][T][V]+ProcNative[S][T][V]).Size()*W[T];
                    if(Contribution<Max*.05) continue;
                    const double Factor=FMath::Pow(Safety[V],Contribution/Max);SourceNative[S][T][V]*=Factor;ProcNative[S][T][V]*=Factor;if(Detail)Detail->SourceSafety[S][T][V]*=Factor;
                }
            }
        }
        if(ContinuousSafety)UE_LOG(LogTemp,Display,TEXT("GLUTE_CONTINUITY iteration=%d maximum_attenuation_change=%.9g"),Iteration+1,ContinuityReduction);
        Audit->SetNumberField(TEXT("blend_safety_iterations"),Iteration+1);if(!Changed) break;if(Iteration==63) return Fail(TEXT("Target-aware blended safety failed convergence"));
    }
    FTargetFields SkinningResidual;
    bool SourceOperator=false;(*Policy)->TryGetBoolField(TEXT("use_source_skinning_reference"),SourceOperator);
    FTargetFields OperatorReference;
    if(SourceOperator || SourceFieldOperator)
    {
        if(!Verified || !(*Source)->HasField(TEXT("bulge_preparation"))) return Fail(TEXT("Source operator requires initialized source bulge weights"));
        OperatorReference.SetNum(2);
        for(int32 S=0;S<2;++S)
        {
            OperatorReference[S].SetNum(P.Targets.Num());
            for(int32 T=1;T<P.Targets.Num();++T)
            {
                const FVector D=P.Targets[T].Degrees,Flex(D.X,0,0);
                const auto FullPose=Pose(I,G,Bind,S==0?D:FVector::ZeroVector,S==1?D:FVector::ZeroVector,true);
                const auto FlexPose=Pose(I,G,Bind,S==0?Flex:FVector::ZeroVector,S==1?Flex:FVector::ZeroVector,true);
                auto Reference=RotationBlend(I,Bind,FullPose);const auto FlexReference=RotationBlend(I,Bind,FlexPose);
                const auto SourceFlex=Apply(SourceReference(I,Ids,Bind,**Source,Flex,I.Bones[G.Sides[S].ThighBone].Name.ToString()),I.Vertices,true);
                for(int32 V=0;V<Reference.Num();++V)Reference[V]+=SourceFlex[V]-FlexReference[V];
                // The verified source field already owns flexion. Do not apply
                // the same operator again through the synthetic DQ residual.
                if(SourceFieldOperator && FMath::Abs(D.X)>1.e-8)Reference=Apply(Skin(I,I.Influences,Bind,FullPose),I.Vertices,true);
                OperatorReference[S][T]=MoveTemp(Reference);
            }
        }
        Audit->SetStringField(TEXT("skinning_reference"),TEXT("Initialized source thigh-X operator plus rotation-blend non-flexion residual; experimental compound-pose extension"));
    }
    if(!BuildSkinningResidual(I,G,P,Bind,Aliases,SourceNative,ProcNative,SkinningResidual,*Audit,Detail,(SourceOperator||SourceFieldOperator)?&OperatorReference:nullptr)) return Fail(TEXT("Skinning residual safety failed; output not published"));
    P.Provenance+=TEXT(" Local rotation-blend minus LBS residual; fixed source contribution; baked GPU Morphs.");
    for(int32 Side=0;Side<2;++Side) for(int32 Target=1;Target<P.Targets.Num();++Target)
    {
        const auto& S=G.Sides[Side];const auto& SS=Surface[Side];const FTransform Anchor=S.AnchorLocal*Bind[S.PelvisBone];auto& St=Stage[Side][Target];const auto& Skin=TargetSkin[Side][Target];auto Native=SourceNative[Side][Target];for(int32 V=0;V<Ids.Num();++V) Native[V]+=ProcNative[Side][Target][V]+SkinningResidual[Side][Target][V];
        if(Detail){Detail->Project(Side,Target,TEXT("source"),SourceNative[Side][Target],Skin,Anchor);Detail->Project(Side,Target,TEXT("procedural"),ProcNative[Side][Target],Skin,Anchor);Detail->Project(Side,Target,TEXT("residual"),SkinningResidual[Side][Target],Skin,Anchor);Detail->Project(Side,Target,TEXT("final"),Native,Skin,Anchor);Detail->Scalar(Detail->Key(Side,Target,TEXT("source_procedural_safety")),Detail->SourceSafety[Side][Target]);Detail->Scalar(Detail->Key(Side,Target,TEXT("residual_safety")),Detail->ResidualSafety[Side][Target]);}
        const auto Residual=Local(Apply(Skin,SkinningResidual[Side][Target]),Anchor);
        St->SetObjectField(TEXT("12_skinning_residual"),VamGluteAudit::Regions(Residual,SS,Ids));
        const auto SF=Local(Apply(Skin,SourceNative[Side][Target]),Anchor),PF=Local(Apply(Skin,ProcNative[Side][Target]),Anchor),Final=Local(Apply(Skin,Native),Anchor);
        St->SetObjectField(TEXT("08_source_after_target_safety"),VamGluteAudit::Regions(SF,SS,Ids));St->SetObjectField(TEXT("09_procedural_after_target_safety"),VamGluteAudit::Regions(PF,SS,Ids));St->SetObjectField(TEXT("10_final_preskin"),VamGluteAudit::Regions(Native,SS,Ids));St->SetObjectField(TEXT("11_final_posed_surface"),VamGluteAudit::Regions(Final,SS,Ids));
        auto Entry=MakeShared<FJsonObject>();Entry->SetStringField(TEXT("side"),S.Side.ToString());Entry->SetStringField(TEXT("target"),P.Targets[Target].Name.ToString());Entry->SetNumberField(TEXT("source_driver_weight"),FMath::Clamp(P.Targets[Target].Degrees.X/100.,0.,1.));Entry->SetObjectField(TEXT("stages"),St);Entries.Add(MakeShared<FJsonValueObject>(Entry));
        auto Stats=[&](const TCHAR* Name){return St->GetObjectField(Name)->GetObjectField(TEXT("whole"));};FVamGluteCorrectiveDiagnostic DD;DD.Side=Side;DD.Target=Target;DD.RawP95=Stats(TEXT("01_raw_source"))->GetNumberField(TEXT("p95"));DD.AdaptedP95=Stats(TEXT("02_mapped_adapted_source"))->GetNumberField(TEXT("p95"));DD.SourceRms=Stats(TEXT("08_source_after_target_safety"))->GetNumberField(TEXT("rms"));DD.ProceduralRms=Stats(TEXT("09_procedural_after_target_safety"))->GetNumberField(TEXT("rms"));DD.FinalP95=Stats(TEXT("11_final_posed_surface"))->GetNumberField(TEXT("p95"));DD.AffectedVertices=int32(Stats(TEXT("11_final_posed_surface"))->GetNumberField(TEXT("affected_vertices")));
        const double Ref=Stats(TEXT("reference_source_posed"))->GetNumberField(TEXT("rms")),Pre=Stats(TEXT("03_side_localized_source"))->GetNumberField(TEXT("rms"));DD.AttenuationRatio=Ref>1.e-12?DD.SourceRms/Ref:0;DD.SafetyLoss=Pre>1.e-12?1-DD.SourceRms/Pre:0;DD.SkinningResidualRms=Stats(TEXT("12_skinning_residual"))->GetNumberField(TEXT("rms"));
        const double SmoothBefore=Stats(TEXT("05_procedural_masked"))->GetNumberField(TEXT("rms"));DD.SmoothingLoss=SmoothBefore>1.e-12?1-Stats(TEXT("06_procedural_smoothed"))->GetNumberField(TEXT("rms"))/SmoothBefore:0;
        TSet<int32> Seen;for(int32 V=0;V<Ids.Num();++V) if(Used.Contains(V) && SS.RegionWeights[V]>.001 && !Seen.Contains(Ids[V])) { Seen.Add(Ids[V]);if(Seen.Num()%4==0) { DD.Positions.Add(S.RegionPoints[V]);DD.Raw.Add(RawFields[Side][Target][V]);DD.Adapted.Add(AdaptFields[Side][Target][V]);DD.Procedural.Add(PF[V]);DD.SkinningResidual.Add(Residual[V]);DD.Final.Add(Final[V]); }if(Verified && Target==1 && SourceMap.Contains(Ids[V])) ++P.ReusedSourceVertexCount; }
        FVamGluteCorrectiveReplay Replay;Replay.Side=Side;Replay.Target=Target;Replay.PosedDelta=Apply(Skin,Native);P.AuthoredReplay.Add(MoveTemp(Replay));
        P.Diagnostics.Add(MoveTemp(DD));AddMorph(I,P,S,Side,Target,Native,Skin,Anchor);
    }
    TArray<TSharedPtr<FJsonValue>> Bilateral;
    if(Verified) for(double Flex:{0.,30.,60.,90.,100.})
    {
        const FVector Degrees(Flex,0,0);FVamHipSidePose H;H.FlexionExtension=Flex*PI/180.;const auto W=VamGluteCorrective::Weights(P,H);const auto Skin=VamGluteGeometry::Skin(I,I.Influences,Bind,Pose(I,G,Bind,Degrees,Degrees,true));const auto RefSkin=SourceReference(I,Ids,Bind,**Source,Degrees);const auto Base=Apply(Skin,I.Vertices,true),RefBase=Apply(RefSkin,I.Vertices,true);
        FDeltaField Delta,RefDelta;Delta.SetNumZeroed(Ids.Num());RefDelta=Delta;
        for(int32 Side=0;Side<2;++Side) for(int32 Target=1;Target<W.Num();++Target) for(int32 V=0;V<Ids.Num();++V) Delta[V]+=(SourceNative[Side][Target][V]+ProcNative[Side][Target][V])*W[Target];
        // The bilateral source driver is exactly average(-Flex,-Flex)/-100.
        for(int32 V=0;V<Ids.Num();++V) if(const auto* D=SourceMap.Find(Ids[V]))
        {
            const double Sum=G.Sides[0].RegionWeights[V]+G.Sides[1].RegionWeights[V];FVector Adapted=FVector::ZeroVector;
            for(int32 Side=0;Side<2;++Side) { const FTransform A=G.Sides[Side].AnchorLocal*Bind[G.Sides[Side].PelvisBone];Adapted+=A.TransformVectorNoScale(A.InverseTransformVectorNoScale(*D)*SourceScales[Side])*(Sum>1.e-12?G.Sides[Side].RegionWeights[V]/Sum:.5); }
            RefDelta[V]=Adapted*FMath::Clamp(Flex/100.,0.,1.);
        }
        RefDelta=Apply(RefSkin,RefDelta);const auto Actual=Apply(Skin,Delta);const auto NativeBase=Apply(VamGluteGeometry::Skin(I,OriginalInfluences,Bind,Pose(I,G,Bind,Degrees,Degrees,false)),I.Vertices,true);FDeltaField NativeError=Actual;for(int32 V=0;V<Ids.Num();++V) NativeError[V]=NativeBase[V]-RefBase[V];FDeltaField Difference=Actual,Absolute=Actual,Baseline=Actual;for(int32 V=0;V<Ids.Num();++V) { Difference[V]-=RefDelta[V];Absolute[V]+=Base[V]-RefBase[V]-RefDelta[V];Baseline[V]=Base[V]-RefBase[V]; }
        auto R=MakeShared<FJsonObject>();R->SetStringField(TEXT("scope"),TEXT("source/procedural only; excludes independent skinning residual"));R->SetNumberField(TEXT("flexion"),Flex);R->SetNumberField(TEXT("driver_weight"),FMath::Clamp(Flex/100.,0.,1.));
        for(int32 Side=0;Side<2;++Side) { auto Q=MakeShared<FJsonObject>();Q->SetObjectField(TEXT("reference_delta"),VamGluteAudit::Regions(RefDelta,Surface[Side],Ids));Q->SetObjectField(TEXT("actual_delta"),VamGluteAudit::Regions(Actual,Surface[Side],Ids));Q->SetObjectField(TEXT("delta_error"),VamGluteAudit::Regions(Difference,Surface[Side],Ids));Q->SetObjectField(TEXT("absolute_surface_error"),VamGluteAudit::Regions(Absolute,Surface[Side],Ids));Q->SetObjectField(TEXT("source_native_lbs_error"),VamGluteAudit::Regions(NativeError,Surface[Side],Ids));Q->SetObjectField(TEXT("baseline_skinning_error"),VamGluteAudit::Regions(Baseline,Surface[Side],Ids));R->SetObjectField(Side==0?TEXT("L"):TEXT("R"),Q); }Bilateral.Add(MakeShared<FJsonValueObject>(R));
    }
    Audit->SetArrayField(TEXT("targets"),Entries);Audit->SetArrayField(TEXT("bilateral"),Bilateral);P.FidelityAuditJson=VamGluteAudit::Json(Audit);
    auto Reference=MakeShared<FJsonObject>();Reference->SetStringField(TEXT("family"),P.SkeletonFamily);Reference->SetStringField(TEXT("scope"),TEXT("Source equation surface moments, not copied vertex deltas"));Reference->SetArrayField(TEXT("sides"),ReferenceSides);if(Verified) Reference->SetObjectField(TEXT("source_hashes"),(*Source)->GetObjectField(TEXT("source_hashes")));P.FamilyReferenceJson=Verified?VamGluteAudit::Json(Reference):VamGluteAudit::Json(*Calibration);
    P.MarkPackageDirty();return P.IsValidProfile()?true:Fail(TEXT("Invalid generated corrective profile"));
}

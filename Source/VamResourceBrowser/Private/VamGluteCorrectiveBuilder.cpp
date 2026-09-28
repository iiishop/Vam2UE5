#include "VamGluteCorrectiveBuilder.h"
#include <cmath>
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/SecureHash.h"

namespace
{
FVector Vec(const TArray<TSharedPtr<FJsonValue>>& A) { return FVector(A[0]->AsNumber(),A[1]->AsNumber(),A[2]->AsNumber()); }
FQuat PoseRotation(const FVamGluteSide& S,const FVector& Degrees)
{
    const FVector R=Degrees*(PI/180.);const FVector A=S.FemurAxisInAnchor;
    FVector Swing(S.SideSign*R.Y,R.X,0);Swing.Z=-(Swing.X*A.X+Swing.Y*A.Y)/A.Z;
    const double L=Swing.Size();return (L>1.e-12?FQuat(Swing/L,L):FQuat::Identity)*FQuat(A,S.SideSign*R.Z);
}
}
bool VamGluteCorrectiveBuilder::Build(FVamNativeMeshInput& I,const TArray<int32>& SourceIds,const UVamGluteStructureProfile& G,UVamGluteCorrectiveProfile& P,const FString& FamilyJson,FString& Error)
{
    auto Fail=[&](const FString& Why){Error=Why;return false;};TSharedPtr<FJsonObject> Family;
    if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(FamilyJson),Family)) return Fail(TEXT("Invalid corrective family JSON"));
    const TSharedPtr<FJsonObject>* Policy=nullptr;
    if(!Family->TryGetObjectField(TEXT("glute_corrective"),Policy)) return Fail(TEXT("Unsupported family: Glute corrective policy missing"));
    if((*Policy)->GetIntegerField(TEXT("version"))!=1) return Fail(TEXT("Unsupported corrective policy version"));
    P.SkeletonFamily=G.SkeletonFamily;P.SourceTopologyIdentity=G.SourceTopologyIdentity;
    FString PolicyText;FJsonSerializer::Serialize((*Policy).ToSharedRef(),TJsonWriterFactory<>::Create(&PolicyText));P.FamilyPolicyIdentity=FMD5::HashAnsiString(*PolicyText);
    P.MetricDegrees=Vec((*Policy)->GetArrayField(TEXT("metric_degrees")));P.BlendWidth=(*Policy)->GetNumberField(TEXT("blend_width"));P.MaximumDimensionFraction=(*Policy)->GetNumberField(TEXT("maximum_dimension_fraction"));
    for(const auto& Value:(*Policy)->GetArrayField(TEXT("targets"))) { auto O=Value->AsObject();FVamGlutePoseTarget T;T.Name=FName(*O->GetStringField(TEXT("name")));T.Degrees=Vec(O->GetArrayField(TEXT("degrees")));P.Targets.Add(T); }
    TMap<int32,FVector> SourceDeltas;TArray<FVector> SourcePositions;
    const TSharedPtr<FJsonObject>* Source=nullptr;
    P.Provenance=TEXT("Procedural family curvature residual; source driver unavailable. G05 scaffold remains unchanged.");
    if(Family->TryGetObjectField(TEXT("glute_corrective_source"),Source))
    {
        P.Provenance=(*Source)->GetStringField(TEXT("reason"))+TEXT("; family curvature residual; source deltas regionalized, dimension adapted and driven by cardinal pose space.");
        for(const auto& Value:(*Source)->GetArrayField(TEXT("deltas"))) { const auto& D=Value->AsArray();SourceDeltas.Add(int32(D[0]->AsNumber()),FVector(D[1]->AsNumber(),D[2]->AsNumber(),D[3]->AsNumber())); }
        const TArray<TSharedPtr<FJsonValue>>* Vertices=nullptr;if((*Source)->TryGetArrayField(TEXT("neutral_vertices_cm"),Vertices)) for(const auto& V:*Vertices) SourcePositions.Add(Vec(V->AsArray()));
    }
    if(SourceIds.Num()!=I.Vertices.Num()) return Fail(TEXT("Corrective topology mapping mismatch"));
    TArray<FTransform> Bind;for(const auto& B:I.Bones) Bind.Add(B.Parent<0?B.LocalBind:B.LocalBind*Bind[B.Parent]);
    TArray<TArray<FVamBuildInfluence>> Weights;Weights.SetNum(I.Vertices.Num());for(const auto& W:I.Influences) Weights[W.Vertex].Add(W);
    TArray<TArray<int32>> Adj;Adj.SetNum(I.Vertices.Num());TMap<int32,TArray<int32>> Weld;
    for(int32 V=0;V<I.Vertices.Num();++V) Weld.FindOrAdd(SourceIds[V]).Add(V);
    for(int32 T=0;T<I.Triangles.Num();T+=3) for(int32 K=0;K<3;++K) { int32 A=I.Triangles[T+K],B=I.Triangles[T+(K+1)%3];Adj[A].AddUnique(B);Adj[B].AddUnique(A); }
    for(const auto& Pair:Weld) for(int32 A:Pair.Value) for(int32 B:Pair.Value) if(A!=B) Adj[A].AddUnique(B);
    for(int32 Side=0;Side<2;++Side)
    {
        const auto& S=G.Sides[Side];P.BuildDimensions.Add(S.Dimensions);const FTransform Anchor=S.AnchorLocal*Bind[S.PelvisBone];
        if(FMath::Abs(S.FemurAxisInAnchor.Z)<.2) return Fail(TEXT("Unsupported femur frame for corrective targets"));
        FVector SourceMean=FVector::ZeroVector,CurrentMean=FVector::ZeroVector,SourceVar=FVector::ZeroVector,CurrentVar=FVector::ZeroVector;double Sum=0;
        for(int32 V=0;V<I.Vertices.Num();++V) if(SourcePositions.IsValidIndex(SourceIds[V])) { const double W=S.RegionWeights[V];SourceMean+=Anchor.InverseTransformPosition(SourcePositions[SourceIds[V]])*W;CurrentMean+=S.RegionPoints[V]*W;Sum+=W; }
        if(Sum>0) { SourceMean/=Sum;CurrentMean/=Sum; }
        for(int32 V=0;V<I.Vertices.Num();++V) if(SourcePositions.IsValidIndex(SourceIds[V])) { const FVector A=Anchor.InverseTransformPosition(SourcePositions[SourceIds[V]])-SourceMean,B=S.RegionPoints[V]-CurrentMean;SourceVar+=A*A*S.RegionWeights[V];CurrentVar+=B*B*S.RegionWeights[V]; }
        FVector SourceScale=FVector::OneVector;for(int32 A=0;A<3;++A) if(SourceVar[A]>.001) SourceScale[A]=FMath::Clamp(FMath::Sqrt(CurrentVar[A]/SourceVar[A]),.25,4.);
        // A bounded BUILD-TIME conditioning sweep, not more runtime pose targets.
        // Thin triangles already compressed by the scaffold cannot safely carry
        // the same surface residual as well-conditioned muscle-belly triangles.
        TArray<double> Safety;Safety.Init(1,I.Vertices.Num());
        for(double Flexion:{0.,30.,60.,90.}) for(double Abduction:{-20.,0.,35.}) for(double Rotation:{-25.,0.,30.})
        {
            auto Thigh=S.RestThighInAnchor;Thigh.SetRotation(PoseRotation(S,FVector(Flexion,Abduction,Rotation))*Thigh.GetRotation());
            const auto Structure=VamGluteStructure::Evaluate(G,S,Thigh);auto Pose=Bind;Pose[S.ThighBone]=Thigh*Anchor;
            for(int32 B=S.ThighBone+1;B<I.Bones.Num();++B) if(I.Bones[B].Parent>=0) Pose[B]=I.Bones[B].LocalBind*Pose[I.Bones[B].Parent];
            for(int32 N=0;N<5;++N) Pose[S.Regions[N].BoneIndex]=Structure.Regions[N].Transform*Anchor;
            TArray<FVector> Points;Points.SetNumZeroed(I.Vertices.Num());
            for(int32 V=0;V<I.Vertices.Num();++V) for(const auto& W:Weights[V]) Points[V]+=FVector((Bind[W.Bone].ToInverseMatrixWithScale()*Pose[W.Bone].ToMatrixWithScale()).TransformPosition(I.Vertices[V]))*W.Weight;
            for(int32 T=0;T<I.Triangles.Num();T+=3)
            {
                const int32 A=I.Triangles[T],B=I.Triangles[T+1],C=I.Triangles[T+2];
                if(S.RegionWeights[A]+S.RegionWeights[B]+S.RegionWeights[C]<.001) continue;
                const double RestArea=FVector::CrossProduct(I.Vertices[B]-I.Vertices[A],I.Vertices[C]-I.Vertices[A]).Size();
                if(RestArea<1.e-8) continue;
                const double Area=FVector::CrossProduct(Points[B]-Points[A],Points[C]-Points[A]).Size();
                const double Factor=FMath::SmoothStep(.05,.5,Area/RestArea);
                for(int32 V:{A,B,C}) Safety[V]=FMath::Min(Safety[V],Factor);
            }
        }
        for(int32 Pass=0;Pass<6;++Pass) { auto Next=Safety;for(int32 V=0;V<Safety.Num();++V) if(!Adj[V].IsEmpty()) { double Mean=0;for(int32 N:Adj[V]) Mean+=Safety[N];Next[V]=FMath::Min(Safety[V],Mean/Adj[V].Num()); }Safety=MoveTemp(Next); }
        for(int32 Target=1;Target<P.Targets.Num();++Target)
        {
            auto Thigh=S.RestThighInAnchor;Thigh.SetRotation(PoseRotation(S,P.Targets[Target].Degrees)*Thigh.GetRotation());
            const auto Structure=VamGluteStructure::Evaluate(G,S,Thigh);
            auto Posed=Bind;Posed[S.ThighBone]=Thigh*Anchor;
            for(int32 B=S.ThighBone+1;B<I.Bones.Num();++B) if(I.Bones[B].Parent>=0) Posed[B]=I.Bones[B].LocalBind*Posed[I.Bones[B].Parent];
            for(int32 N=0;N<5;++N) Posed[S.Regions[N].BoneIndex]=Structure.Regions[N].Transform*Anchor;
            TArray<FMatrix> InverseSkin;InverseSkin.SetNum(I.Vertices.Num());
            for(int32 V=0;V<I.Vertices.Num();++V)
            {
                FMatrix M=FMatrix::Identity;for(int32 A=0;A<4;++A) for(int32 B=0;B<4;++B) M.M[A][B]=0;
                for(const auto& W:Weights[V]) M+=(Bind[W.Bone].ToInverseMatrixWithScale()*Posed[W.Bone].ToMatrixWithScale())*W.Weight;
                if(FMath::Abs(M.Determinant())<.01 && S.RegionWeights[V]>.001) return Fail(TEXT("Corrective target skin matrix singular; reject unsafe geometry"));
                InverseSkin[V]=FMath::Abs(M.Determinant())<.01?FMatrix::Identity:M.Inverse();
            }
            const FVector Ang=P.Targets[Target].Degrees;
            TArray<FVector> Field;Field.SetNumZeroed(I.Vertices.Num());
            for(int32 V=0;V<I.Vertices.Num();++V)
            {
                const FVector Point=S.RegionPoints[V];const double Region=S.RegionWeights[V];if(Region<1.e-6) continue;
                FVector D=VamGluteCorrective::CurvatureDelta(S,Point,Ang,Structure.FoldState);
                if(const FVector* SourceDelta=SourceDeltas.Find(SourceIds[V])) { D+=Anchor.InverseTransformVectorNoScale(*SourceDelta)*SourceScale*FMath::Clamp(Ang.X/100.,0.,1.);if(Target==1 && Region>.001) ++P.ReusedSourceVertexCount; }
                const double Across=S.SideSign*(Point.Y-S.FoldSemanticMap.MedialInfraglutealAnchor.Y)/FMath::Max(.1,FMath::Abs(S.FoldSemanticMap.LateralFade.Y-S.FoldSemanticMap.MedialInfraglutealAnchor.Y));
                D*=Safety[V]*Region*FMath::SmoothStep(0.,.4,FMath::Max(0.,Across));const double Limit=S.Dimensions.GetMin()*P.MaximumDimensionFraction,L=D.Size();
                Field[V]=L>1.e-12?D*(Limit*std::tanh(L/Limit)/L):D;
            }
            // Diffuse on source topology, weld UV duplicates and preserve a continuous region boundary.
            for(int32 Pass=0;Pass<4;++Pass) { auto Next=Field;for(int32 V=0;V<Field.Num();++V) if(!Adj[V].IsEmpty()) { FVector Mean=FVector::ZeroVector;for(int32 N:Adj[V]) Mean+=Field[N];Next[V]=FMath::Lerp(Field[V],Mean/Adj[V].Num(),.35)*FMath::Min(1.,double(S.RegionWeights[V])*8)*Safety[V]; }Field=MoveTemp(Next); }
            for(int32 Axis=0;Axis<3;++Axis)
            {
                FVamBuildMorph Morph;Morph.Name=FName(*FString::Printf(TEXT("G06_%s_%s_%d"),*S.Side.ToString(),*P.Targets[Target].Name.ToString(),Axis));Morph.Deltas.SetNumZeroed(I.Vertices.Num());
                FVamGluteCorrectiveBasis Basis;Basis.Morph=Morph.Name;Basis.Side=Side;Basis.Target=Target;Basis.Axis=Axis;Basis.RegionalRmsCm.Init(0,5);TArray<double> RegionalWeight;RegionalWeight.Init(0,5);
                for(int32 V=0;V<Field.Num();++V)
                {
                    FVector D=FVector::ZeroVector;D[Axis]=Field[V][Axis];Morph.Deltas[V]=FVector(InverseSkin[V].TransformVector(Anchor.TransformVectorNoScale(D)));
                    Basis.MaximumCm=FMath::Max(Basis.MaximumCm,FMath::Abs(D[Axis]));
                    for(int32 N=0;N<5;++N) { const double W=FMath::Exp(-((S.RegionPoints[V]-S.Regions[N].Rest)/S.Dimensions).SizeSquared()*12)*S.RegionWeights[V];Basis.RegionalRmsCm[N]+=D.SizeSquared()*W;RegionalWeight[N]+=W; }
                    if(V%19==0 && S.RegionWeights[V]>.02) { Basis.DebugPositions.Add(S.RegionPoints[V]);Basis.DebugLocalDeltas.Add(D); }
                }
                if(Basis.MaximumCm<1.e-6) continue;
                for(int32 N=0;N<5;++N) Basis.RegionalRmsCm[N]=FMath::Sqrt(Basis.RegionalRmsCm[N]/FMath::Max(.001,RegionalWeight[N]));
                if(I.Morphs.ContainsByPredicate([&](const FVamBuildMorph& M){return M.Name==Morph.Name;})) return Fail(TEXT("Reserved corrective Morph name collision"));
                TArray<FVector> Before,After;Before.SetNumZeroed(I.Vertices.Num());After.SetNumZeroed(I.Vertices.Num());
                for(int32 T=0;T<I.Triangles.Num();T+=3)
                {
                    const int32 A=I.Triangles[T],B=I.Triangles[T+1],C=I.Triangles[T+2];
                    const FVector N0=FVector::CrossProduct(I.Vertices[B]-I.Vertices[A],I.Vertices[C]-I.Vertices[A]);
                    const FVector N1=FVector::CrossProduct(I.Vertices[B]+Morph.Deltas[B]-I.Vertices[A]-Morph.Deltas[A],I.Vertices[C]+Morph.Deltas[C]-I.Vertices[A]-Morph.Deltas[A]);
                    for(int32 V:{A,B,C}) { Before[V]+=N0;After[V]+=N1; }
                }
                Morph.NormalDeltas.SetNum(I.Vertices.Num());for(int32 V=0;V<I.Vertices.Num();++V) Morph.NormalDeltas[V]=After[V].GetSafeNormal()-Before[V].GetSafeNormal();
                P.Bases.Add(MoveTemp(Basis));I.Morphs.Add(MoveTemp(Morph));
            }
        }
    }
    P.MarkPackageDirty();return P.IsValidProfile()?true:Fail(TEXT("Invalid generated GluteCorrectiveProfile"));
}

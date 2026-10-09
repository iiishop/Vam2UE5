#include "VamLowerBodyContactBuilder.h"
#include "VamBreastContactProfile.h"
#include "VamBreastJiggleBuilder.h"
#include "VamGluteStructureProfile.h"
#include "VamLegJiggleProfile.h"
#include "VamCharacterDefinition.h"
#include "VamNativeBuilder.h"
#include "Engine/SkeletalMesh.h"
#include "FTetWildWrapper.h"

namespace
{
double LowerDet(FVector A,FVector B,FVector C){return FVector::DotProduct(A,FVector::CrossProduct(B,C));}
double LowerSmooth(double V){V=FMath::Clamp(V,0.,1.);return V*V*V*(V*(V*6-15)+10);}
struct FLowerBind {FIntVector4 Parents;FVector4f Weights;double Distance=DBL_MAX;};
FLowerBind LowerBind(FVector X,const TArray<FVector>& Points,const TArray<FIntVector4>& Cells)
{
    FLowerBind Best;
    for(auto T:Cells){const FVector A=Points[T.X],B=Points[T.Y]-A,C=Points[T.Z]-A,D=Points[T.W]-A,Q=X-A;
        const double Den=LowerDet(B,C,D);if(FMath::Abs(Den)<1e-12)continue;
        const double Y=LowerDet(Q,C,D)/Den,Z=LowerDet(B,Q,D)/Den,W=LowerDet(B,C,Q)/Den;
        FVector4f BC(1-Y-Z-W,Y,Z,W);
        if(FMath::Min(FMath::Min(BC.X,BC.Y),FMath::Min(BC.Z,BC.W))>=0)return {T,BC,0};
        const int F[4][3]={{1,2,3},{0,3,2},{0,1,3},{0,2,1}};
        for(const auto& Face:F){const FVector A0=Points[T[Face[0]]],B0=Points[T[Face[1]]],C0=Points[T[Face[2]]];
            const FVector P=FMath::ClosestPointOnTriangleToPoint(X,A0,B0,C0);const double Dist=FVector::DistSquared(P,X);if(Dist>=Best.Distance)continue;
            FVector Bary=FMath::ComputeBaryCentric2D(P,A0,B0,C0);for(int J=0;J<3;++J)Bary[J]=FMath::Max(0.,Bary[J]);Bary/=Bary.X+Bary.Y+Bary.Z;
            BC=FVector4f(0,0,0,0);for(int J=0;J<3;++J)BC[Face[J]]=Bary[J];Best={T,BC,Dist};}}
    return Best;
}
}

FString UVamLowerBodyContactBuilder::Append(UVamCharacterDefinition* Definition,UVamGluteStructureProfile* Glute,UVamLegJiggleProfile* Leg,UVamBreastContactProfile* Profile)
{
    if(!Definition||!Glute||!Leg||!Profile||Glute->Sides.Num()!=2||Leg->Segments.Num()!=4)return TEXT("Lower contact needs bilateral Glute and four Leg source regions");
    if(Glute->SkeletonFamily!=Leg->SkeletonFamily || Glute->SourceTopologyIdentity!=Leg->SourceTopologyIdentity)return TEXT("Lower contact family/topology mismatch");
    if(Profile->EffectiveVolumeCm3.Num()!=2)return TEXT("Append requires an unmodified bilateral breast profile");
    FVamNativeMeshInput Input;FString Error;auto* Mesh=Definition->Body.LoadSynchronous();
    if(!UVamBreastJiggleBuilder::ExtractNative(Mesh,Input,Error))return Error;
    const int NV=Input.Vertices.Num();
    TArray<float> Mask[2];for(int S=0;S<2;++S){if(Glute->Sides[S].RegionWeights.Num()!=NV)return TEXT("Lower contact source correspondence mismatch");Mask[S]=Glute->Sides[S].RegionWeights;}
    for(const auto& Part:Leg->Segments){if(Part.RegionWeights.Num()!=NV||Part.Side<0||Part.Side>1)return TEXT("Lower contact leg correspondence mismatch");for(int V=0;V<NV;++V)Mask[Part.Side][V]=FMath::Max(Mask[Part.Side][V],Part.RegionWeights[V]);}
    TArray<FTransform> Ref;for(auto B:Input.Bones){FTransform T=B.LocalBind;if(B.Parent>=0)T*=Ref[B.Parent];Ref.Add(T);}
    TArray<TMap<int,float>> Skin;Skin.SetNum(NV);for(auto W:Input.Influences)Skin[W.Vertex].FindOrAdd(W.Bone)+=W.Weight;
    const auto RenderMap=UVamNativeBuilder::GetRenderToInputMap(Mesh);if(RenderMap.Num()!=Profile->SurfaceMask.Num())return TEXT("Lower contact render correspondence mismatch");
    // Transactional authoring: failed meshing does not mutate the existing profile.
    auto* P=DuplicateObject<UVamBreastContactProfile>(Profile,GetTransientPackage());
    P->SchemaVersion=5;P->RegionNames={FName("LeftBreast"),FName("RightBreast"),FName("LeftLowerBody"),FName("RightLowerBody")};P->EffectiveVolumeCm3.SetNumZeroed(4);
    for(int Side=0;Side<2;++Side){
        const auto* Thigh=Leg->Segments.FindByPredicate([&](const FVamLegSegment& S){return S.Side==Side&&!S.bCalf;});
        if(!Thigh)return TEXT("Lower contact skeletal segment semantics missing");
        const int Bones[4]={Thigh->Pelvis,Thigh->Thigh,Thigh->Shin,Thigh->Foot};for(int B:Bones)if(!Ref.IsValidIndex(B))return TEXT("Lower contact support bone invalid");
        TArray<FIntVector> SourceFaces;TSet<int> Used;
        for(int T=0;T+2<Input.Triangles.Num();T+=3){FIntVector F(Input.Triangles[T],Input.Triangles[T+1],Input.Triangles[T+2]);
            double W=0,Other=0;for(int J=0;J<3;++J){W+=Mask[Side][F[J]];Other+=Mask[1-Side][F[J]];}
            if(W<=.005 || W<=Other)continue;
            if(FVector::CrossProduct(Input.Vertices[F.Y]-Input.Vertices[F.X],Input.Vertices[F.Z]-Input.Vertices[F.X]).SizeSquared()<1e-12)continue;
            SourceFaces.Add(F);for(int J=0;J<3;++J)Used.Add(F[J]);}
        if(SourceFaces.Num()<16)return TEXT("Lower contact has insufficient source surface evidence");
        TArray<int> Ordered=Used.Array();Ordered.Sort();TMap<int,int> Map;TArray<FVector> Surface,Inner;
        TArray<TMap<int,double>> Support;TMap<FIntVector,int> Weld;
        for(int V:Ordered){const FVector X=Input.Vertices[V];const FIntVector Key(FMath::RoundToInt(X.X*10000),FMath::RoundToInt(X.Y*10000),FMath::RoundToInt(X.Z*10000));if(const int* Existing=Weld.Find(Key)){Map.Add(V,*Existing);continue;}Weld.Add(Key,Surface.Num());FVector Closest=FVector::ZeroVector;double Dist=DBL_MAX,TBest=0;int Best=0;
            for(int J=0;J<3;++J){const FVector A=Ref[Bones[J]].GetLocation(),B=Ref[Bones[J+1]].GetLocation(),D=B-A;const double T=FMath::Clamp(FVector::DotProduct(X-A,D)/FMath::Max(1e-12,D.SizeSquared()),0.,1.);const FVector Q=A+D*T;
                if(FVector::DistSquared(X,Q)<Dist){Dist=FVector::DistSquared(X,Q);Closest=Q;TBest=T;Best=J;}}
            if(Dist<1e-4)return TEXT("Lower contact source intersects skeletal support axis");
            Map.Add(V,Surface.Num());Surface.Add(X);Inner.Add(FMath::Lerp(Closest,X,.45));TMap<int,double> Weights;Weights.Add(Bones[Best],1-TBest);Weights.FindOrAdd(Bones[Best+1])+=TBest;Support.Add(Weights);}
        const int OuterCount=Surface.Num();Surface.Append(Inner);TArray<FIntVector> Envelope;
        struct FEdge {int A=0,B=0,Count=0;};TMap<uint64,FEdge> Edges;
        for(auto F:SourceFaces){for(int J=0;J<3;++J)F[J]=Map[F[J]];Envelope.Add(F);Envelope.Add(FIntVector(F.X+OuterCount,F.Z+OuterCount,F.Y+OuterCount));
            for(int J=0;J<3;++J){int A=F[J],B=F[(J+1)%3];const uint64 K=(uint64(FMath::Min(A,B))<<32)|uint32(FMath::Max(A,B));auto& E=Edges.FindOrAdd(K);E.A=A;E.B=B;++E.Count;}}
        for(const auto& Pair:Edges){const auto& E=Pair.Value;if(E.Count>2)return TEXT("Lower contact source surface is nonmanifold");if(E.Count==1){Envelope.Add(FIntVector(E.B,E.A,E.A+OuterCount));Envelope.Add(FIntVector(E.B,E.A+OuterCount,E.B+OuterCount));}}
        UE::Geometry::FTetWild::FTetMeshParameters Params;Params.IdealEdgeLengthRel=.10;Params.EpsRel=.0005;Params.MaxIts=40;Params.bCoarsen=true;
        TArray<FVector> Points;TArray<FIntVector4> Cells;
        if(!UE::Geometry::FTetWild::ComputeTetMesh(Params,Surface,Envelope,Points,Cells)||Cells.IsEmpty())return TEXT("Lower-body UE fTetWild failed");
        if(Points.Num()>12000 || Cells.Num()>60000)return FString::Printf(TEXT("Lower-body fTetWild exceeded per-side budget: %d particles, %d cells"),Points.Num(),Cells.Num());
        const int Base=P->Particles.Num();
        TArray<int> ClosestFaces;TArray<FVector> Coordinates;
        for(const FVector X:Points){double DOuter=DBL_MAX,DInner=DBL_MAX;int FaceId=INDEX_NONE;FVector Bary;
            for(int F=0;F<SourceFaces.Num();++F){const auto T=SourceFaces[F];const FVector A=Input.Vertices[T.X],B=Input.Vertices[T.Y],C=Input.Vertices[T.Z];const FVector Q=FMath::ClosestPointOnTriangleToPoint(X,A,B,C);const double D=FVector::DistSquared(Q,X);
                if(D<DOuter){DOuter=D;FaceId=F;Bary=FMath::ComputeBaryCentric2D(Q,A,B,C);}
                DInner=FMath::Min(DInner,FVector::DistSquared(X,FMath::ClosestPointOnTriangleToPoint(X,Inner[Map[T.X]],Inner[Map[T.Y]],Inner[Map[T.Z]])));}
            if(FaceId<0)return TEXT("Lower contact attachment projection failed");
            const auto F=SourceFaces[FaceId];for(int J=0;J<3;++J)Bary[J]=FMath::Max(0.,Bary[J]);Bary/=Bary.X+Bary.Y+Bary.Z;
            const double Depth=FMath::Sqrt(DOuter)/(FMath::Sqrt(DOuter)+FMath::Sqrt(DInner)+1e-9);
            FVamBreastContactParticle Particle;Particle.Rest=X;Particle.Side=Side+2;Particle.RootSupport=Depth;Particle.bKinematic=Depth>.98;
            TMap<int,double> W;for(int J=0;J<3;++J){for(auto Pair:Skin[F[J]])W.FindOrAdd(Pair.Key)+=Pair.Value*Bary[J]*(1-Depth);for(auto Pair:Support[Map[F[J]]])W.FindOrAdd(Pair.Key)+=Pair.Value*Bary[J]*Depth;}
            TArray<int> Keys;W.GetKeys(Keys);Keys.Sort();double Sum=0;for(int K:Keys)Sum+=W[K];if(Sum<=0)return TEXT("Lower contact lost attachment support");
            for(int K:Keys)if(W[K]>0){Particle.Bones.Add(K);Particle.Weights.Add(W[K]/Sum);}P->Particles.Add(Particle);ClosestFaces.Add(FaceId);Coordinates.Add(Bary);}
        struct FFace {FIntVector Oriented;int Count=0;};TMap<FIntVector,FFace> Boundary;
        double Volume=0;
        for(auto& T:Cells){double V=LowerDet(Points[T.Y]-Points[T.X],Points[T.Z]-Points[T.X],Points[T.W]-Points[T.X])/6;if(V<0){Swap(T.Z,T.W);V=-V;}if(V<=1e-8)return TEXT("Lower contact fTetWild degenerate cell");Volume+=V;
            FIntVector4 Global=T;for(int J=0;J<4;++J)Global[J]+=Base;P->Tetrahedra.Add(Global);
            const FIntVector Faces[]={FIntVector(Global.Y,Global.Z,Global.W),FIntVector(Global.X,Global.W,Global.Z),FIntVector(Global.X,Global.Y,Global.W),FIntVector(Global.X,Global.Z,Global.Y)};
            for(auto F:Faces){auto K=F;if(K.X>K.Y)Swap(K.X,K.Y);if(K.Y>K.Z)Swap(K.Y,K.Z);if(K.X>K.Y)Swap(K.X,K.Y);auto& Entry=Boundary.FindOrAdd(K);Entry.Oriented=F;++Entry.Count;}}
        for(auto Pair:Boundary){if(Pair.Value.Count==1)P->BoundaryTriangles.Add(Pair.Value.Oriented);else if(Pair.Value.Count!=2)return TEXT("Lower contact fTetWild nonmanifold output");}
        P->EffectiveVolumeCm3[Side+2]=Volume;
        TMap<int,FLowerBind> Binding;
        for(int V:Ordered)if(Mask[Side][V]>.005 && Mask[Side][V]>Mask[1-Side][V]){const auto B=LowerBind(Input.Vertices[V],Points,Cells);if(!FMath::IsFinite(B.Distance)||B.Distance>.25)return FString::Printf(TEXT("Lower contact surface escaped envelope: vertex %d distance %g cm weight %g, points %d cells %d"),V,FMath::Sqrt(B.Distance),Mask[Side][V],Points.Num(),Cells.Num());Binding.Add(V,B);}
        for(int R=0;R<RenderMap.Num();++R)if(const auto* B=Binding.Find(RenderMap[R])){const int V=RenderMap[R];const double W=LowerSmooth((Mask[Side][V]-.005)/.35);if(W<=P->SurfaceMask[R])continue;
            auto Parents=B->Parents;FVector Embedded=FVector::ZeroVector;for(int J=0;J<4;++J){Embedded+=Points[Parents[J]]*B->Weights[J];Parents[J]+=Base;}
            P->SurfaceParents[R]=Parents;P->SurfaceWeights[R]=B->Weights;P->SurfaceMask[R]=W;P->SurfaceOffsets[R]=FVector3f(Input.Vertices[V]-Embedded);}
        for(auto& M:P->Morphs)M.ParticleDeltas.SetNumZeroed(P->Particles.Num());
        for(const auto& Morph:Input.Morphs){const auto* Parameter=Definition->Parameters.FindByPredicate([&](const FVamMorphParameter& V){return V.Target==Morph.Name;});if(!Parameter || Parameter->Group==TEXT("Expression"))continue;auto* M=P->Morphs.FindByPredicate([&](const FVamBreastContactMorph& V){return V.Parameter==Morph.Name;});
            if(!M){FVamBreastContactMorph New;New.Parameter=Morph.Name;New.ParticleDeltas.Init(FVector::ZeroVector,P->Particles.Num());M=&P->Morphs.Add_GetRef(MoveTemp(New));}
            for(int I=0;I<Points.Num();++I){const auto F=SourceFaces[ClosestFaces[I]];const auto W=Coordinates[I];M->ParticleDeltas[Base+I]=Morph.Deltas[F.X]*W.X+Morph.Deltas[F.Y]*W.Y+Morph.Deltas[F.Z]*W.Z;}}
        UE_LOG(LogTemp,Display,TEXT("LOWER_CONTACT side=%d particles=%d tets=%d volume_cm3=%g source_faces=%d"),Side,Points.Num(),Cells.Num(),Volume,SourceFaces.Num());
    }
    P->bResidualOnlySurface=true;P->BuildAlgorithmVersion+=TEXT("-lower-connected-ftetwild-v1");
    P->RegionProvenance+=TEXT("; lower body: Glute/Leg continuous source support union; source surface shell closed onto skeleton-supported inner envelope; inner radial fraction .45 is an engineering proxy, not segmented anatomy; UE fTetWild edge=.10 eps=.0005");
    Error=P->ValidateData();if(!Error.IsEmpty())return Error;
    Profile->SchemaVersion=P->SchemaVersion;Profile->RegionNames=P->RegionNames;Profile->Particles=MoveTemp(P->Particles);Profile->Tetrahedra=MoveTemp(P->Tetrahedra);Profile->BoundaryTriangles=MoveTemp(P->BoundaryTriangles);Profile->Morphs=MoveTemp(P->Morphs);Profile->EffectiveVolumeCm3=P->EffectiveVolumeCm3;
    Profile->SurfaceParents=MoveTemp(P->SurfaceParents);Profile->SurfaceWeights=MoveTemp(P->SurfaceWeights);Profile->SurfaceMask=MoveTemp(P->SurfaceMask);Profile->SurfaceOffsets=MoveTemp(P->SurfaceOffsets);Profile->bResidualOnlySurface=true;Profile->BuildAlgorithmVersion=P->BuildAlgorithmVersion;Profile->RegionProvenance=P->RegionProvenance;Profile->MarkPackageDirty();return {};
}

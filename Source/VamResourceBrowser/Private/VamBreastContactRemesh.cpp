#include "VamBreastContactBuilder.h"
#include "VamBreastContactProfile.h"
#include "FTetWildWrapper.h"
#include "HAL/IConsoleManager.h"

namespace
{
TAutoConsoleVariable<int32> CVarContactFTetWild(TEXT("vam.Contact.BuildFTetWild"),0,
    TEXT("Build experimental fTetWild contact cages for new character outputs. Existing assets are unchanged."));
double D(const FVector& A,const FVector& B,const FVector& C){return FVector::DotProduct(A,FVector::CrossProduct(B,C));}
struct FBinding{FIntVector4 T;FVector4f W;FVector Point;double Error2=DBL_MAX;};
FBinding Bind(const FVector& P,int32 Side,const TArray<FVamBreastContactParticle>& V,const TArray<FIntVector4>& Cells)
{
    FBinding Best;
    for(const auto& T:Cells)
    {
        if(V[T[0]].Side!=Side)continue;
        const FVector A=V[T[0]].Rest,B=V[T[1]].Rest-A,C=V[T[2]].Rest-A,E=V[T[3]].Rest-A,Q=P-A;
        const double Den=D(B,C,E);if(FMath::Abs(Den)<1.e-12)continue;
        const double Y=D(Q,C,E)/Den,Z=D(B,Q,E)/Den,W=D(B,C,Q)/Den;
        FVector4f Weights(1-Y-Z-W,Y,Z,W);
        if(FMath::Min(FMath::Min(Weights.X,Weights.Y),FMath::Min(Weights.Z,Weights.W))>=0)
            return {T,Weights,P,0};
        // Exact closest point on the tetrahedron boundary, never extrapolated weights.
        const int32 Faces[4][3]={{1,2,3},{0,3,2},{0,1,3},{0,2,1}};
        for(const auto& F:Faces)
        {
            const FVector X=V[T[F[0]]].Rest,U=V[T[F[1]]].Rest,R=V[T[F[2]]].Rest;
            const FVector Closest=FMath::ClosestPointOnTriangleToPoint(P,X,U,R);const double Dist=FVector::DistSquared(P,Closest);
            if(Dist>=Best.Error2)continue;
            FVector BC=FMath::ComputeBaryCentric2D(Closest,X,U,R);for(int32 J=0;J<3;++J)BC[J]=FMath::Max(0.,BC[J]);BC/=BC.X+BC.Y+BC.Z;
            Weights=FVector4f(0,0,0,0);for(int32 J=0;J<3;++J)Weights[F[J]]=BC[J];Best={T,Weights,Closest,Dist};
        }
    }
    return Best;
}
}

FString UVamBreastContactBuilder::RemeshFTetWild(UVamBreastContactProfile* P)
{
    if(!CVarContactFTetWild.GetValueOnGameThread())return {};
    if(!P)return TEXT("fTetWild profile absent");
    const auto OldV=P->Particles;const auto OldT=P->Tetrahedra;const auto OldM=P->Morphs;
    TArray<FVamBreastContactParticle> NewV;TArray<FIntVector4> NewT;TArray<FIntVector> NewF;
    TArray<FBinding> Transfer;double MaxTransfer=0,MinQuality=1;
    for(int32 Side=0;Side<2;++Side)
    {
        TArray<FVector> Surface;TArray<FIntVector> Faces;TMap<int32,int32> Remap;
        for(auto F:P->BoundaryTriangles)if(OldV[F.X].Side==Side)
        {for(int32 J=0;J<3;++J){const int32 Id=F[J];if(!Remap.Contains(Id)){Remap.Add(Id,Surface.Num());Surface.Add(OldV[Id].Rest);}F[J]=Remap[Id];}Faces.Add(F);}
        UE::Geometry::FTetWild::FTetMeshParameters Params;Params.IdealEdgeLengthRel=.15;Params.EpsRel=.001;Params.MaxIts=40;
        TArray<FVector> Points;TArray<FIntVector4> Cells;
        if(!UE::Geometry::FTetWild::ComputeTetMesh(Params,Surface,Faces,Points,Cells) || Cells.IsEmpty())return TEXT("UE fTetWild generation failed");
        if(Points.Num()>4096 || Cells.Num()>20000)return TEXT("fTetWild contact budget exceeded");
        const int32 Base=NewV.Num();FBox Box(ForceInit);for(auto X:Surface)Box+=X;
        const double Tolerance=FMath::Max(.01,Box.GetSize().Size()*.003);
        for(auto X:Points)
        {
            const FBinding B=Bind(X,Side,OldV,OldT);
            if(!FMath::IsFinite(B.Error2) || B.Error2>Tolerance*Tolerance)return TEXT("fTetWild attachment transfer escaped source envelope");
            MaxTransfer=FMath::Max(MaxTransfer,FMath::Sqrt(B.Error2));Transfer.Add(B);
            FVamBreastContactParticle N;N.Rest=X;N.Side=Side;TMap<int32,double> Skin;double KinematicWeight=0;
            for(int32 J=0;J<4;++J){const auto& O=OldV[B.T[J]];const double W=B.W[J];
                N.RootSupport+=O.RootSupport*W;N.NippleSupport+=O.NippleSupport*W;KinematicWeight+=(O.bKinematic?W:0);
                for(int32 K=0;K<O.Bones.Num();++K)Skin.FindOrAdd(O.Bones[K])+=O.Weights[K]*W;}
            N.bKinematic=KinematicWeight>.95;
            N.RootSupport=FMath::Clamp(N.RootSupport,0.f,1.f);N.NippleSupport=FMath::Clamp(N.NippleSupport,0.f,1.f);
            TArray<int32> Keys;Skin.GetKeys(Keys);Keys.Sort();double Total=0;for(int32 K:Keys)Total+=Skin[K];
            if(Total<=0)return TEXT("fTetWild attachment has zero mass");
            for(int32 K:Keys)if(Skin[K]>0){N.Bones.Add(K);N.Weights.Add(Skin[K]/Total);}NewV.Add(MoveTemp(N));
        }
        struct FFace{FIntVector Oriented;int32 Count=0;};TMap<FIntVector,FFace> FaceCounts;
        double Volume=0;
        for(auto T:Cells)
        {
            double V=D(Points[T[1]]-Points[T[0]],Points[T[2]]-Points[T[0]],Points[T[3]]-Points[T[0]])/6;
            if(V<0){Swap(T[2],T[3]);V=-V;}if(V<=1.e-8)return TEXT("fTetWild generated degenerate contact cell");
            double Edges=0;for(int32 A=0;A<4;++A)for(int32 B=A+1;B<4;++B)Edges+=FVector::DistSquared(Points[T[A]],Points[T[B]]);
            MinQuality=FMath::Min(MinQuality,12*FMath::Pow(3*V,2./3.)/Edges);Volume+=V;
            for(int32 J=0;J<4;++J)T[J]+=Base;NewT.Add(T);
            const FIntVector TF[]={FIntVector(T[1],T[2],T[3]),FIntVector(T[0],T[3],T[2]),FIntVector(T[0],T[1],T[3]),FIntVector(T[0],T[2],T[1])};
            for(auto F:TF){auto Key=F;if(Key.X>Key.Y)Swap(Key.X,Key.Y);if(Key.Y>Key.Z)Swap(Key.Y,Key.Z);if(Key.X>Key.Y)Swap(Key.X,Key.Y);auto& Entry=FaceCounts.FindOrAdd(Key);Entry.Oriented=F;++Entry.Count;}
        }
        if(FMath::Abs(Volume/P->EffectiveVolumeCm3[Side]-1)>.01)return TEXT("fTetWild initial envelope volume changed more than 1 percent");
        TArray<FIntVector> Keys;FaceCounts.GetKeys(Keys);Keys.Sort([](const FIntVector& A,const FIntVector& B){return A.X!=B.X?A.X<B.X:A.Y!=B.Y?A.Y<B.Y:A.Z<B.Z;});
        for(auto Key:Keys){const auto& F=FaceCounts[Key];if(F.Count==1)NewF.Add(F.Oriented);else if(F.Count!=2)return TEXT("fTetWild nonmanifold boundary");}
    }
    if(MinQuality<.05)return TEXT("fTetWild contact quality below accepted minimum");
    auto Parents=P->SurfaceParents;auto Weights=P->SurfaceWeights;auto Offsets=P->SurfaceOffsets;double MaxOffset=0;
    for(int32 I=0;I<Parents.Num();++I)if(P->SurfaceMask[I]>0)
    {
        const int32 Side=OldV[Parents[I][0]].Side;FVector X(Offsets[I]);for(int32 J=0;J<4;++J)X+=OldV[Parents[I][J]].Rest*Weights[I][J];
        const auto B=Bind(X,Side,NewV,NewT);if(B.Error2>.01)return TEXT("fTetWild surface embedding exceeds 1 mm tolerance");
        Parents[I]=B.T;Weights[I]=B.W;FVector Bound=FVector::ZeroVector;for(int32 J=0;J<4;++J)Bound+=NewV[B.T[J]].Rest*B.W[J];
        Offsets[I]=FVector3f(X-Bound);MaxOffset=FMath::Max(MaxOffset,FVector(Offsets[I]).Size());
    }
    auto Morphs=OldM;for(int32 M=0;M<Morphs.Num();++M){Morphs[M].ParticleDeltas.SetNum(NewV.Num());for(int32 I=0;I<NewV.Num();++I){FVector Delta=FVector::ZeroVector;for(int32 J=0;J<4;++J)Delta+=OldM[M].ParticleDeltas[Transfer[I].T[J]]*Transfer[I].W[J];Morphs[M].ParticleDeltas[I]=Delta;}}
    // Commit only after geometry and all transfers succeed.
    P->Particles=MoveTemp(NewV);P->Tetrahedra=MoveTemp(NewT);P->BoundaryTriangles=MoveTemp(NewF);P->Morphs=MoveTemp(Morphs);
    P->SurfaceParents=MoveTemp(Parents);P->SurfaceWeights=MoveTemp(Weights);P->SurfaceOffsets=MoveTemp(Offsets);
    P->EffectiveVolumeCm3.Init(0,2);TArray<FVector> Rest;for(auto& V:P->Particles)Rest.Add(V.Rest);
    for(auto T:P->Tetrahedra)P->EffectiveVolumeCm3[P->Particles[T[0]].Side]+=P->SignedTetVolume(Rest,T);
    P->BuildAlgorithmVersion=TEXT("breast-contact-c5-ue-ftetwild-v1");
    P->RegionProvenance+=FString::Printf(TEXT("; final simulation mesh UE fTetWild edge=.15 eps=.001 iterations=40; source cage only used as transfer domain; min quality %.6f max attachment transfer %.6f cm max render offset %.6f cm"),MinQuality,MaxTransfer,MaxOffset);
    return P->ValidateData();
}

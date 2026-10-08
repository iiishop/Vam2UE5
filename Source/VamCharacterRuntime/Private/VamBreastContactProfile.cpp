#include "VamBreastContactProfile.h"

double UVamBreastContactProfile::SignedTetVolume(const TArray<FVector>& P,const FIntVector4& T)
{
    return FVector::DotProduct(P[T[1]]-P[T[0]],FVector::CrossProduct(P[T[2]]-P[T[0]],P[T[3]]-P[T[0]]))/6.;
}

double UVamBreastContactProfile::CompressionCorrection(double RestVolume,double CurrentVolume) const
{
    if(RestVolume<=1.e-12 || !FMath::IsFinite(CurrentVolume))return 0;
    const double J=CurrentVolume/RestVolume;
    // C1 at J=1: no rest force or tensile bulk penalty. A gradual resistance
    // transmits compression before a cell reaches the emergency inversion floor.
    // This is a bounded projection model, not a measured constitutive law.
    const double Compression=FMath::Max(0.,1-J);
    const double Continuous=RestVolume*LocalCompressionResistance*Compression*Compression/FMath::Max(.2,J);
    return FMath::Max(Continuous,FMath::Max(0.,RestVolume*CompressionBarrierRatio-CurrentVolume));
}

double UVamBreastContactProfile::ProbeFront(const TArray<FVector>& Positions,const FTransform& Frame,const FVector& Center,double Radius,bool bPlaten,bool bBodyOnly,int32 Side) const
{
    double Front=-DBL_MAX;
    // Surface-aware first touch. A sphere or plate can touch inside a triangle
    // before any vertex reaches it; vertex-only onset incorrectly starts penetrated.
    for(const auto& T:BoundaryTriangles)
    {
        if(Particles[T.X].Side!=Side)continue;
        if(bBodyOnly && FMath::Max3(Particles[T.X].NippleSupport,Particles[T.Y].NippleSupport,Particles[T.Z].NippleSupport)>=.1)continue;
        FVector P[3];for(int32 J=0;J<3;++J)P[J]=Frame.InverseTransformPosition(Positions[T[J]]);
        if(P[0].ContainsNaN() || P[1].ContainsNaN() || P[2].ContainsNaN())continue;
        if(bPlaten)
        {
            TArray<FVector,TInlineAllocator<8>> Polygon;for(const auto& V:P)Polygon.Add(V);
            for(int32 Axis=1;Axis<=2;++Axis)for(int32 Sign:{-1,1})
            {
                TArray<FVector,TInlineAllocator<8>> Next;
                for(int32 I=0;I<Polygon.Num();++I){const FVector A=Polygon[I],B=Polygon[(I+1)%Polygon.Num()];
                    const double DA=(A[Axis]-Center[Axis])*Sign-Radius,DB=(B[Axis]-Center[Axis])*Sign-Radius;
                    if(DA<=0)Next.Add(A);if((DA<0 && DB>0)||(DA>0 && DB<0))Next.Add(A+(B-A)*(DA/(DA-DB)));}
                Polygon=MoveTemp(Next);
            }
            for(const auto& V:Polygon)Front=FMath::Max(Front,V.X);
        }
        else
        {
            FVector Flat[3];for(int32 J=0;J<3;++J)Flat[J]=FVector(0,P[J].Y-Center.Y,P[J].Z-Center.Z);
            if(FVector::CrossProduct(Flat[1]-Flat[0],Flat[2]-Flat[0]).SizeSquared()<1.e-12)continue;
            const FVector Q=FMath::ClosestPointOnTriangleToPoint(FVector::ZeroVector,Flat[0],Flat[1],Flat[2]);
            if(Q.SizeSquared()>=Radius*Radius)continue;
            const FVector W=FMath::ComputeBaryCentric2D(Q,Flat[0],Flat[1],Flat[2]);
            double Low=P[0].X*W.X+P[1].X*W.Y+P[2].X*W.Z,High=FMath::Max3(P[0].X,P[1].X,P[2].X)+Radius;
            for(int32 I=0;I<20;++I){const double Mid=(Low+High)*.5;const FVector C(Mid,Center.Y,Center.Z);
                if(FVector::DistSquared(C,FMath::ClosestPointOnTriangleToPoint(C,P[0],P[1],P[2]))<Radius*Radius)Low=Mid;else High=Mid;}
            Front=FMath::Max(Front,High-Radius);
        }
    }
    return Front;
}

FString UVamBreastContactProfile::ValidateData() const
{
    if((SchemaVersion<1 || SchemaVersion>4) || SourceTopologyIdentity.IsEmpty() || BindSignature.IsEmpty() || SkeletonFamily.IsEmpty())
        return TEXT("Contact profile identity missing or unsupported");
    if(Particles.IsEmpty() || Tetrahedra.IsEmpty() || EffectiveVolumeCm3.Num()!=2)
        return TEXT("Contact cage is incomplete");
    if(!FMath::IsFinite(YoungModulusPa) || YoungModulusPa<=0 || !FMath::IsFinite(PoissonRatio) || PoissonRatio<0 || PoissonRatio>=.5 ||
       !FMath::IsFinite(DensityKgPerCm3) || DensityKgPerCm3<=0 || !FMath::IsFinite(AttachmentStiffness) || AttachmentStiffness<0 ||
       !FMath::IsFinite(FixedStepSeconds) || FixedStepSeconds<=0 || FixedStepSeconds>.02 || MaxSubsteps<1 || MaxSubsteps>32 || SolverIterations<1 || SolverIterations>64)
        return TEXT("Contact material/timestep outside supported range");
    if(SchemaVersion>=2 && (!FMath::IsFinite(SurfaceSpacingRadiusFraction) || SurfaceSpacingRadiusFraction<=0 ||
        MaxAdditionalSurfaceParticlesPerSide<0 || MaxAdditionalSurfaceParticlesPerSide>256 ||
        !FMath::IsFinite(CompressionBarrierRatio) || CompressionBarrierRatio<=0 || CompressionBarrierRatio>=1 ||
        !FMath::IsFinite(SurfaceMinimumStretch) || SurfaceMinimumStretch<=0 || SurfaceMinimumStretch>=1 ||
        !FMath::IsFinite(SurfaceMaximumStretch) || SurfaceMaximumStretch<=1 ||
        !FMath::IsFinite(ConstraintRelaxation) || ConstraintRelaxation<=0 || ConstraintRelaxation>1 ||
        !FMath::IsFinite(DebugPressRadiusFraction) || DebugPressRadiusFraction<=0 || DebugPressRadiusFraction>1 ||
        !FMath::IsFinite(MinimumMovableMassKg) || MinimumMovableMassKg<=.0001 ||
        !FMath::IsFinite(NippleShapePreservation) || NippleShapePreservation<0 || NippleShapePreservation>1 ||
        !FMath::IsFinite(NippleAllowedStrain) || NippleAllowedStrain<=0 || NippleAllowedStrain>=.3))
        return TEXT("Invalid C2 calibration");
    if(!FMath::IsFinite(LocalCompressionResistance) || LocalCompressionResistance<0 || LocalCompressionResistance>1 || !FMath::IsFinite(SurfaceBending) || SurfaceBending<0 || SurfaceBending>1)
        return TEXT("Invalid local compression resistance");
    if(!FMath::IsFinite(GPUSkinBendingRelaxation) || GPUSkinBendingRelaxation<0 || GPUSkinBendingRelaxation>1)
        return TEXT("Invalid GPU skin bending relaxation");
    TArray<FVector> Rest;
    for(const auto& P:Particles)
    {
        if(P.Rest.ContainsNaN() || P.Side<0 || P.Side>1 || P.Bones.IsEmpty() || P.Bones.Num()!=P.Weights.Num() ||
           !FMath::IsFinite(P.RootSupport) || P.RootSupport<0 || P.RootSupport>1 ||
           !FMath::IsFinite(P.NippleSupport) || P.NippleSupport<0 || P.NippleSupport>1) return TEXT("Invalid contact particle");
        double Sum=0;
        for(int32 I=0;I<P.Bones.Num();++I)
        {
            if(P.Bones[I]<0 || !FMath::IsFinite(P.Weights[I]) || P.Weights[I]<0) return TEXT("Invalid contact attachment");
            Sum+=P.Weights[I];
        }
        if(FMath::Abs(Sum-1)>1.e-4) return TEXT("Contact attachment is not normalized");
        Rest.Add(P.Rest);
    }
    for(const auto& T:Tetrahedra)
    {
        for(int32 J=0;J<4;++J) if(!Particles.IsValidIndex(T[J])) return TEXT("Contact tetrahedron index invalid");
        for(int32 J=1;J<4;++J) if(Particles[T[J]].Side!=Particles[T[0]].Side) return TEXT("Contact tetrahedron bridges independent sides");
        if(SignedTetVolume(Rest,T)<=1.e-8) return TEXT("Contact tetrahedron inverted or degenerate at rest");
    }
    if(SurfaceParents.IsEmpty() || SurfaceParents.Num()!=SurfaceWeights.Num() || SurfaceParents.Num()!=SurfaceMask.Num() || SurfaceParents.Num()!=SurfaceOffsets.Num())
        return TEXT("Contact render bindings incomplete");
    for(int32 V=0;V<SurfaceMask.Num();++V)
    {
        if(!FMath::IsFinite(SurfaceMask[V]) || SurfaceMask[V]<0 || SurfaceMask[V]>1) return TEXT("Invalid contact surface mask");
        if(SurfaceMask[V]==0) continue;
        double Sum=0;
        for(int32 J=0;J<4;++J)
        {
            if(!Particles.IsValidIndex(SurfaceParents[V][J]) || !FMath::IsFinite(SurfaceWeights[V][J]) || SurfaceWeights[V][J]<-1.e-4)
                return TEXT("Invalid contact surface parent/weight");
            Sum+=SurfaceWeights[V][J];
        }
        if(FMath::Abs(Sum-1)>1.e-4 || SurfaceOffsets[V].ContainsNaN()) return TEXT("Invalid contact surface embedding");
    }
    for(const auto& M:Morphs)
    {
        if(M.Parameter.IsNone() || !FMath::IsFinite(M.Baseline) || M.ParticleDeltas.Num()!=Particles.Num()) return TEXT("Contact Morph identity mismatch");
        for(const auto& D:M.ParticleDeltas) if(D.ContainsNaN()) return TEXT("Non-finite contact Morph delta");
    }
    return FString();
}

bool UVamBreastContactProfile::MeasureVolume(const TArray<FVector>& Rest,const TArray<FVector>& Current,TArray<FVamBreastContactVolumeState>& Out) const
{
    Out.Reset();
    if(Rest.Num()!=Particles.Num() || Current.Num()!=Particles.Num()) return false;
    for(const auto& P:Rest) if(P.ContainsNaN()) return false;
    for(const auto& P:Current) if(P.ContainsNaN()) return false;
    Out.SetNum(2);
    for(const auto& T:Tetrahedra)
    {
        for(int32 J=0;J<4;++J) if(!Particles.IsValidIndex(T[J])) return false;
        const int32 Side=Particles[T[0]].Side;if(!Out.IsValidIndex(Side)) return false;
        const double R=SignedTetVolume(Rest,T),V=SignedTetVolume(Current,T);
        if(R<=1.e-8 || !FMath::IsFinite(R) || !FMath::IsFinite(V)) return false;
        auto& S=Out[Side];S.RestVolumeCm3+=R;S.CurrentVolumeCm3+=V;
        S.MaximumTetRatio=FMath::Max(S.MaximumTetRatio,V/R);S.MinimumTetRatio=FMath::Min(S.MinimumTetRatio,V/R);S.InvertedTetrahedra+=V<=0?1:0;
    }
    for(int32 Side=0;Side<2;++Side){TArray<int32> Nodes;
        for(int32 I=0;I<Particles.Num();++I)if(Particles[I].Side==Side && Particles[I].NippleSupport>.25 && !Particles[I].bKinematic)Nodes.Add(I);
        Nodes.Sort([&](int32 A,int32 B){return Particles[A].NippleSupport==Particles[B].NippleSupport?A<B:Particles[A].NippleSupport>Particles[B].NippleSupport;});if(Nodes.Num()>32)Nodes.SetNum(32);
        double Sum=0,Weight=0;auto& S=Out[Side];
        for(int32 I=0;I<Nodes.Num();++I)for(int32 J=I+1;J<Nodes.Num();++J){const int32 A=Nodes[I],B=Nodes[J];const double L=FVector::Distance(Rest[A],Rest[B]);if(L<1.e-6)continue;
            const double W=FMath::Min(Particles[A].NippleSupport,Particles[B].NippleSupport);Sum+=W*FMath::Square(FVector::Distance(Current[A],Current[B])/L-1);Weight+=W;++S.NippleShapePairCount;}
        S.NippleShapeRmsStrain=Weight>0?FMath::Sqrt(Sum/Weight):0;}
    for(const auto& F:BoundaryTriangles) for(int32 J=0;J<3;++J)
    {
        const int32 A=F[J],B=F[(J+1)%3];if(!Particles.IsValidIndex(A) || !Particles.IsValidIndex(B))return false;
        const double L=FVector::Distance(Rest[A],Rest[B]);if(L<=1.e-8)continue;
        const double Ratio=FVector::Distance(Current[A],Current[B])/L;auto& S=Out[Particles[A].Side];
        if(Ratio>S.MaximumSurfaceStretch)S.WorstStretchRestLengthCm=L;
        S.MaximumEdgeExtensionCm=FMath::Max(S.MaximumEdgeExtensionCm,L*(Ratio-1));
        S.MaximumSurfaceStretch=FMath::Max(S.MaximumSurfaceStretch,Ratio);S.MinimumSurfaceStretch=FMath::Min(S.MinimumSurfaceStretch,Ratio);
    }
    for(auto& S:Out)
    {
        if(S.RestVolumeCm3<=1.e-8) return false;
        S.RelativeVolumeError=S.CurrentVolumeCm3/S.RestVolumeCm3-1;
    }
    return true;
}

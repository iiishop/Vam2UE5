#pragma once
#include "VamNativeBuilder.h"
#include "VamGluteStructure.h"
#include "BoneWeights.h"

namespace VamGluteGeometry
{
using FDeltaField=TArray<FVector>;
inline FQuat Rotation(const FVamGluteSide& S,const FVector& D)
{
    const FVector R=D*(PI/180.),A=S.FemurAxisInAnchor;FVector V(S.SideSign*R.Y,R.X,0);V.Z=-(V.X*A.X+V.Y*A.Y)/A.Z;
    const double L=V.Size();return (L>1.e-12?FQuat(V/L,L):FQuat::Identity)*FQuat(A,S.SideSign*R.Z);
}
inline TArray<FTransform> Pose(const FVamNativeMeshInput& I,const UVamGluteStructureProfile& G,const TArray<FTransform>& Bind,const FVector& Left,const FVector& Right,bool Scaffold)
{
    const FVector D[]={Left,Right};auto P=Bind;
    for(int32 B=0;B<I.Bones.Num();++B)
    {
        P[B]=I.Bones[B].Parent<0?I.Bones[B].LocalBind:I.Bones[B].LocalBind*P[I.Bones[B].Parent];
        for(int32 S=0;S<2;++S) if(B==G.Sides[S].ThighBone) { const auto& Side=G.Sides[S];auto T=Side.RestThighInAnchor;T.SetRotation(Rotation(Side,D[S])*T.GetRotation());P[B]=T*Side.AnchorLocal*P[Side.PelvisBone]; }
    }
    if(Scaffold) for(int32 S=0;S<2;++S)
    {
        const auto& Side=G.Sides[S];const FTransform Anchor=Side.AnchorLocal*P[Side.PelvisBone];const auto State=VamGluteStructure::Evaluate(G,Side,P[Side.ThighBone].GetRelativeTransform(Anchor));
        for(int32 N=0;N<5;++N) P[Side.Regions[N].BoneIndex]=State.Regions[N].Transform*Anchor;
    }return P;
}
inline TArray<FMatrix> Skin(const FVamNativeMeshInput& I,const TArray<FVamBuildInfluence>& Influences,const TArray<FTransform>& Bind,const TArray<FTransform>& P)
{
    TArray<FMatrix> B;for(int32 N=0;N<Bind.Num();++N) B.Add(Bind[N].ToInverseMatrixWithScale()*P[N].ToMatrixWithScale());
    TArray<FMatrix> M;M.SetNum(I.Vertices.Num());for(auto& X:M) for(int32 A=0;A<4;++A) for(int32 C=0;C<4;++C) X.M[A][C]=0;
    // Match NativeBuilder's FBoneWeight quantization and normalization before
    // conditioning. Small weight errors are material on compressed triangles.
    TArray<TArray<UE::AnimationCore::FBoneWeight>> Quantized;Quantized.SetNum(I.Vertices.Num());for(const auto& W:Influences) Quantized[W.Vertex].Add(UE::AnimationCore::FBoneWeight(W.Bone,W.Weight));
    for(int32 V=0;V<Quantized.Num();++V) for(const auto& W:UE::AnimationCore::FBoneWeights::Create(MakeArrayView(Quantized[V]))) M[V]+=B[W.GetBoneIndex()]*W.GetWeight();return M;
}
inline FDeltaField Apply(const TArray<FMatrix>& M,const FDeltaField& F,bool Position=false)
{ FDeltaField O;O.SetNum(F.Num());for(int32 V=0;V<F.Num();++V) O[V]=Position?FVector(M[V].TransformPosition(F[V])):FVector(M[V].TransformVector(F[V]));return O; }
inline void Weld(FDeltaField& F,const TMap<int32,TArray<int32>>& Aliases)
{ for(const auto& P:Aliases) { FVector Mean=FVector::ZeroVector;for(int32 V:P.Value) Mean+=F[V];Mean/=P.Value.Num();for(int32 V:P.Value) F[V]=Mean; } }
// Diffusion and boundary multiplication are separate. A constrained moment
// projection restores weighted vector mean and RMS without removing boundaries.
inline void Smooth(FDeltaField& F,const TArray<TArray<int32>>& Adj,const TArray<double>& Boundary,const TArray<double>& W,const TMap<int32,TArray<int32>>& Aliases)
{
    auto Mask=Boundary;for(const auto& P:Aliases) { double Mean=0;for(int32 V:P.Value) Mean+=Mask[V];Mean/=P.Value.Num();for(int32 V:P.Value) Mask[V]=Mean; }
    const auto Initial=F;double Total=0,E0=0,MeanMask=0;FVector M0=FVector::ZeroVector;
    for(int32 V=0;V<F.Num();++V) { Total+=W[V];M0+=F[V]*W[V];E0+=F[V].SizeSquared()*W[V];MeanMask+=Mask[V]*W[V]; }
    if(Total<1.e-12 || E0<1.e-20 || MeanMask<1.e-12) return;M0/=Total;E0/=Total;MeanMask/=Total;
    for(int32 Pass=0;Pass<4;++Pass) { auto Next=F;for(int32 V=0;V<F.Num();++V) if(!Adj[V].IsEmpty()) { FVector Mean=FVector::ZeroVector;for(int32 N:Adj[V]) Mean+=F[N];Next[V]=FMath::Lerp(F[V],Mean/Adj[V].Num(),.35); }F=MoveTemp(Next);Weld(F,Aliases); }
    FVector M1=FVector::ZeroVector;for(int32 V=0;V<F.Num();++V) { F[V]*=Mask[V];M1+=F[V]*W[V]; }M1/=Total;
    double A=0,B=0,C=-E0;for(int32 V=0;V<F.Num();++V) { F[V]-=Mask[V]*M1/MeanMask;const FVector Center=Mask[V]*M0/MeanMask;A+=W[V]*F[V].SizeSquared()/Total;B+=2*W[V]*FVector::DotProduct(F[V],Center)/Total;C+=W[V]*Center.SizeSquared()/Total; }
    const double Discriminant=B*B-4*A*C;if(A<1.e-20 || Discriminant<0) { F=Initial;return; }
    const double Scale=(-B+FMath::Sqrt(Discriminant))/(2*A);if(!FMath::IsFinite(Scale) || Scale<0) { F=Initial;return; }
    for(int32 V=0;V<F.Num();++V) F[V]=F[V]*Scale+Mask[V]*M0/MeanMask;Weld(F,Aliases);
}
inline bool TriangleSafe(const FVector& A,const FVector& B,const FVector& C,const FVector& DA,const FVector& DB,const FVector& DC,double Scale=1,double Margin=0)
{
    const FVector N0=FVector::CrossProduct(B-A,C-A),N1=FVector::CrossProduct(B+DB*Scale-A-DA*Scale,C+DC*Scale-A-DA*Scale);
    return !N1.ContainsNaN() && N1.Size()<FMath::Max(.01,N0.Size()*5)*(1-Margin) && (N0.SizeSquared()<1.e-16 || FVector::DotProduct(N0,N1)>Margin*N0.SizeSquared());
}
// Safety is computed from the actual target displacement, never from a global
// worst-case baseline compression field. Existing 5x area / 3x edge bounds stay.
inline bool Condition(const FVamNativeMeshInput& I,const TArray<FMatrix>& Skin,const FDeltaField& Native,const TMap<int32,TArray<int32>>& Aliases,TArray<double>& Safety,const FDeltaField* Fixed=nullptr)
{
    const auto Base=Apply(Skin,I.Vertices,true),D=Apply(Skin,Native);Safety.Init(1,Native.Num());for(int32 V=0;V<Native.Num();++V) if(!Native[V].IsNearlyZero(1.e-8) && FMath::Abs(Skin[V].Determinant())<.01) Safety[V]=0;
    FDeltaField F;F.SetNumZeroed(Native.Num());if(Fixed) F=Apply(Skin,*Fixed);
    for(int32 Pass=0;Pass<80;++Pass)
    {
        auto Next=Safety;bool Good=true;
        for(int32 T=0;T<I.Triangles.Num();T+=3)
        {
            const int32 A=I.Triangles[T],B=I.Triangles[T+1],C=I.Triangles[T+2];const FVector DA=D[A]*Safety[A],DB=D[B]*Safety[B],DC=D[C]*Safety[C];
            auto Safe=[&](double Scale){return TriangleSafe(Base[A],Base[B],Base[C],F[A]+DA*Scale,F[B]+DB*Scale,F[C]+DC*Scale,1,.05);};
            if(!Safe(1))
            {
                if(Fixed && !Safe(0)) return false;
                Good=false;double Lo=0,Hi=1;for(int32 N=0;N<30;++N) { const double M=(Lo+Hi)*.5;if(Safe(M)) Lo=M;else Hi=M; }
                for(int32 V:{A,B,C}) Next[V]=FMath::Min(Next[V],Safety[V]*Lo*.99);
            }
            for(int32 K=0;K<3;++K)
            {
                const int32 U=I.Triangles[T+K],V=I.Triangles[T+(K+1)%3];const double Limit=FMath::Max(.15,(I.Vertices[U]-I.Vertices[V]).Size()*3);const FVector Offset=Fixed?(*Fixed)[U]-(*Fixed)[V]:FVector::ZeroVector,Adjust=Native[U]*Safety[U]-Native[V]*Safety[V];
                if((Offset+Adjust).Size()>=Limit) { Good=false;double Lo=0,Hi=1;if(Offset.Size()>=Limit) return false;for(int32 N=0;N<30;++N) { const double M=(Lo+Hi)*.5;if((Offset+Adjust*M).Size()<Limit) Lo=M;else Hi=M; }const double Factor=Lo*.99;Next[U]=FMath::Min(Next[U],Safety[U]*Factor);Next[V]=FMath::Min(Next[V],Safety[V]*Factor); }
            }
        }
        if(Good) return true;
        for(const auto& Pair:Aliases) { double M=1;for(int32 V:Pair.Value) M=FMath::Min(M,Next[V]);for(int32 V:Pair.Value) Next[V]=M; }
        Safety=MoveTemp(Next);
    }return false;
}
}

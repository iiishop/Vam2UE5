#pragma once
#include "VamGluteCorrectiveGeometry.h"

namespace VamGluteGeometry
{
// Build-time geometric reference, not an anatomical ground truth. Normalized
// dual-quaternion blending (Kavan et al., TOG 2008, algorithm 1), plus the SAME
// weighted affine residual used by LBS. This preserves scaffold scale instead
// of silently replacing the G0.5 structural model with rigid skinning.
inline FDeltaField RotationBlend(const FVamNativeMeshInput& I,const TArray<FTransform>& Bind,const TArray<FTransform>& Pose)
{
    TArray<FMatrix> Matrices;TArray<FQuat> Real,Dual;
    for(int32 B=0;B<Bind.Num();++B)
    {
        const FMatrix M=Bind[B].ToInverseMatrixWithScale()*Pose[B].ToMatrixWithScale();
        const FQuat Q=(Pose[B].GetRotation()*Bind[B].GetRotation().Inverse()).GetNormalized();
        const FVector T=M.GetOrigin();Matrices.Add(M);Real.Add(Q);Dual.Add((FQuat(T.X,T.Y,T.Z,0)*Q)*.5);
    }
    TArray<TArray<UE::AnimationCore::FBoneWeight>> Influences;Influences.SetNum(I.Vertices.Num());
    for(const auto& W:I.Influences) Influences[W.Vertex].Add(UE::AnimationCore::FBoneWeight(W.Bone,W.Weight));
    FDeltaField Result;Result.SetNum(I.Vertices.Num());
    for(int32 V=0;V<I.Vertices.Num();++V)
    {
        const auto Weights=UE::AnimationCore::FBoneWeights::Create(MakeArrayView(Influences[V]));
        int32 Reference=0;double Maximum=-1;for(const auto& W:Weights) if(W.GetWeight()>Maximum) { Maximum=W.GetWeight();Reference=W.GetBoneIndex(); }
        FQuat R(0,0,0,0),D(0,0,0,0);FVector Affine=FVector::ZeroVector;
        for(const auto& W:Weights)
        {
            const int32 B=W.GetBoneIndex();const double A=W.GetWeight(),Sign=(Real[B]|Real[Reference])<0?-1:1;
            R=R+Real[B]*(A*Sign);D=D+Dual[B]*(A*Sign);
            Affine+=(FVector(Matrices[B].TransformPosition(I.Vertices[V]))-Real[B].RotateVector(I.Vertices[V])-Matrices[B].GetOrigin())*A;
        }
        const double L=R.Size();if(L<1.e-8) { Result[V]=I.Vertices[V];continue; }
        R=R*(1/L);D=D*(1/L);const FQuat T=(D*R.Inverse())*2;
        Result[V]=R.RotateVector(I.Vertices[V])+FVector(T.X,T.Y,T.Z)+Affine;
    }
    return Result;
}
}

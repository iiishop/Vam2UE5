#pragma once
#include "VamGluteRotationBlend.h"
#include "VamGluteCorrectiveProfile.h"
#include "VamGluteCorrectiveAudit.h"
#include <cmath>

namespace VamGluteGeometry
{
using FTargetFields=TArray<TArray<FDeltaField>>;
inline FVector SafetyProbe(const UVamGluteCorrectiveProfile& P,int32 Step)
{
    if(Step<P.Targets.Num()) return P.Targets[Step].Degrees;
    if(Step<18) return FVector((Step-10)*12,20*FMath::Sin(double(Step)),20*FMath::Cos(double(Step)));
    const int32 K=(Step-18)%36;return FVector(Step>=54?(Step<63?-40.:130.):(K/9)*30.,(K/3)%3==0?-20.:(K/3)%3==1?0.:35.,K%3==0?-25.:K%3==1?0.:30.);
}
// Separate from source Morph fidelity. Safety can reduce this residual, never
// the already validated source/procedural fields supplied as Fixed.
inline bool BuildSkinningResidual(const FVamNativeMeshInput& I,const UVamGluteStructureProfile& G,const UVamGluteCorrectiveProfile& P,const TArray<FTransform>& Bind,const TMap<int32,TArray<int32>>& Aliases,const FTargetFields& Source,const FTargetFields& Procedural,FTargetFields& Residual,FJsonObject& Audit)
{
    Residual.SetNum(2);double MaxRegion=0;for(int32 V=0;V<I.Vertices.Num();++V) MaxRegion=FMath::Max(MaxRegion,double(G.Sides[0].RegionWeights[V]+G.Sides[1].RegionWeights[V]));
    for(int32 S=0;S<2;++S)
    {
        Residual[S].SetNum(P.Targets.Num());
        for(int32 T=1;T<P.Targets.Num();++T)
        {
            const FVector D=P.Targets[T].Degrees;const auto PoseCS=Pose(I,G,Bind,S==0?D:FVector::ZeroVector,S==1?D:FVector::ZeroVector,true);
            const auto M=Skin(I,I.Influences,Bind,PoseCS);const auto Base=Apply(M,I.Vertices,true),Reference=RotationBlend(I,Bind,PoseCS);auto& R=Residual[S][T];R.SetNumZeroed(I.Vertices.Num());
            for(int32 V=0;V<R.Num();++V)
            {
                const double Both=G.Sides[0].RegionWeights[V]+G.Sides[1].RegionWeights[V];
                const double Mask=Both>1.e-12?G.Sides[S].RegionWeights[V]/Both*FMath::SmoothStep(0.,MaxRegion*.1,Both):0;
                if(Mask<1.e-12 || FMath::Abs(M[V].Determinant())<.01) continue;
                R[V]=FVector(M[V].Inverse().TransformVector((Reference[V]-Base[V])*Mask));
                // Bound the pre-skin field using the character's measured
                // dimensions, including the fixed contribution. Never amplify.
                const double Remaining=FMath::Max(0.,.3*G.Sides[S].Dimensions.GetMin()-(Source[S][T][V]+Procedural[S][T][V]).Size()),L=R[V].Size();
                if(L>1.e-12) R[V]*=Remaining>1.e-12?Remaining*std::tanh(L/Remaining)/L:0;
            }
            Weld(R,Aliases);
        }
    }
    for(int32 Iteration=0;Iteration<64;++Iteration)
    {
        bool Changed=false;
        // Runtime Shape scales the AP/ML/SI bases independently. Their inverse
        // skin transforms can have larger edge differences than their sum.
        // Condition each exported basis as well, keeping its old field fixed.
        for(int32 S=0;S<2;++S) for(int32 T=1;T<P.Targets.Num();++T)
        {
            const FVector Degrees=P.Targets[T].Degrees;const auto M=Skin(I,I.Influences,Bind,Pose(I,G,Bind,S==0?Degrees:FVector::ZeroVector,S==1?Degrees:FVector::ZeroVector,true));
            const FTransform Anchor=G.Sides[S].AnchorLocal*Bind[G.Sides[S].PelvisBone];TArray<double> Factors;Factors.Init(1,I.Vertices.Num());
            for(int32 Axis=0;Axis<3;++Axis)
            {
                FDeltaField Fixed,Adjust;Fixed.SetNumZeroed(I.Vertices.Num());Adjust=Fixed;
                for(int32 V=0;V<Fixed.Num();++V)
                {
                    if(FMath::Abs(M[V].Determinant())<.01) continue;
                    auto Project=[&](const FVector& Native){const FVector Posed=Anchor.InverseTransformVectorNoScale(FVector(M[V].TransformVector(Native)));FVector A=FVector::ZeroVector;A[Axis]=Posed[Axis];return FVector(M[V].Inverse().TransformVector(Anchor.TransformVectorNoScale(A)));};
                    Fixed[V]=Project(Source[S][T][V]+Procedural[S][T][V]);Adjust[V]=Project(Residual[S][T][V]);
                }
                for(int32 Triangle=0;Triangle<I.Triangles.Num();Triangle+=3) for(int32 K=0;K<3;++K)
                {
                    const int32 U=I.Triangles[Triangle+K],V=I.Triangles[Triangle+(K+1)%3];const double Limit=FMath::Max(.15,(I.Vertices[U]-I.Vertices[V]).Size()*3);const FVector F=Fixed[U]-Fixed[V],A=Adjust[U]-Adjust[V];
                    if((F+A).Size()<Limit) continue;if(F.Size()>=Limit) return false;
                    double Lo=0,Hi=1;for(int32 N=0;N<30;++N) { const double Mid=(Lo+Hi)*.5;if((F+A*Mid).Size()<Limit) Lo=Mid;else Hi=Mid; }
                    Factors[U]=FMath::Min(Factors[U],Lo*.99);Factors[V]=FMath::Min(Factors[V],Lo*.99);Changed=true;
                }
            }
            for(const auto& Pair:Aliases) { double F=1;for(int32 V:Pair.Value) F=FMath::Min(F,Factors[V]);for(int32 V:Pair.Value) Factors[V]=F; }
            for(int32 V=0;V<Factors.Num();++V) Residual[S][T][V]*=Factors[V];
        }
        for(int32 Side=0;Side<3;++Side) for(int32 Step=0;Step<72;++Step)
        {
            const FVector D=SafetyProbe(P,Step);FVamHipSidePose H;H.FlexionExtension=D.X*PI/180.;H.AbductionAdduction=D.Y*PI/180.;H.ExternalInternalRotation=D.Z*PI/180.;const auto W=VamGluteCorrective::Weights(P,H);
            const auto M=Skin(I,I.Influences,Bind,Pose(I,G,Bind,Side!=1?D:FVector::ZeroVector,Side!=0?D:FVector::ZeroVector,true));
            FDeltaField Fixed,Adjust;Fixed.SetNumZeroed(I.Vertices.Num());Adjust=Fixed;
            for(int32 S=0;S<2;++S) if(Side==2 || S==Side) for(int32 T=1;T<W.Num();++T) for(int32 V=0;V<Fixed.Num();++V) { Fixed[V]+=(Source[S][T][V]+Procedural[S][T][V])*W[T];Adjust[V]+=Residual[S][T][V]*W[T]; }
            TArray<double> Safety;if(!Condition(I,M,Adjust,Aliases,Safety,&Fixed)) return false;
            for(int32 V=0;V<Fixed.Num();++V) if(Safety[V]<.999999)
            {
                double Maximum=0;for(int32 S=0;S<2;++S) if(Side==2 || S==Side) for(int32 T=1;T<W.Num();++T) Maximum=FMath::Max(Maximum,Residual[S][T][V].Size()*W[T]);
                if(Maximum<1.e-16) continue;Changed=true;
                for(int32 S=0;S<2;++S) if(Side==2 || S==Side) for(int32 T=1;T<W.Num();++T) { const double C=Residual[S][T][V].Size()*W[T];if(C>=Maximum*.05) Residual[S][T][V]*=FMath::Pow(Safety[V],C/Maximum); }
            }
        }
        Audit.SetNumberField(TEXT("skinning_residual_safety_iterations"),Iteration+1);
        if(!Changed) return true;
    }
    return false;
}
}

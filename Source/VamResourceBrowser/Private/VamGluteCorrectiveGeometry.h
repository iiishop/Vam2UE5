#pragma once
#include "VamNativeBuilder.h"
#include "VamGluteStructure.h"
#include "BoneWeights.h"
#include <queue>

namespace VamGluteGeometry
{
using FDeltaField=TArray<FVector>;
// Opt-in build diagnostics only; input indices are resolved from source IDs by
// the caller. This records the predicate that first constrains a probe point.
inline FString SafetyTraceContext;
inline TSet<int32> SafetyTraceVertices;
inline bool FeasibleSafetyGrowth=false;
// Largest attenuation field below the safety solution with a bounded spatial
// derivative. Only follow mesh edges and proven source aliases, never proximity.
// The attenuation gradient has an explicit bind-edge displacement budget
// to the correction gradient. Existing geometric safety is rechecked afterwards.
inline void ContinuousAttenuation(const FVamNativeMeshInput& I,const FDeltaField& Source,const FDeltaField& Proc,const TMap<int32,TArray<int32>>& Aliases,TArray<double>& A,double EdgeBudget=.25)
{
    struct FArc { int32 Vertex; double Cost; };
    TArray<TArray<FArc>> Graph;Graph.SetNum(A.Num());
    for(int32 T=0;T<I.Triangles.Num();T+=3)for(int32 K=0;K<3;++K)
    {
        const int32 U=I.Triangles[T+K],V=I.Triangles[T+(K+1)%3];
        const double Amplitude=FMath::Max(Source[U].Size()+Proc[U].Size(),Source[V].Size()+Proc[V].Size());
        const double Cost=(I.Vertices[U]-I.Vertices[V]).Size()*EdgeBudget/FMath::Max(1.e-8,Amplitude);
        Graph[U].Add({V,Cost});Graph[V].Add({U,Cost});
    }
    for(const auto& Pair:Aliases)for(int32 V:Pair.Value)if(V!=Pair.Value[0])
    { Graph[V].Add({Pair.Value[0],0});Graph[Pair.Value[0]].Add({V,0}); }
    using FEntry=std::pair<double,int32>;
    std::priority_queue<FEntry,std::vector<FEntry>,std::greater<FEntry>> Queue;
    for(int32 V=0;V<A.Num();++V)Queue.push({A[V],V});
    while(!Queue.empty())
    {
        const auto [Value,U]=Queue.top();Queue.pop();if(Value>A[U]+1.e-12)continue;
        for(const auto& Edge:Graph[U])if(A[Edge.Vertex]>Value+Edge.Cost+1.e-12)
        { A[Edge.Vertex]=Value+Edge.Cost;Queue.push({A[Edge.Vertex],Edge.Vertex}); }
    }
}
// Fair only deformation-induced sharp edges. Smooth the displacement differential,
// not the body surface, preserving the rest anatomy and untouched target regions.
inline bool RelaxIntroducedFolds(const FVamNativeMeshInput& I,const TArray<FMatrix>& Skin,const FDeltaField& Source,FDeltaField& Proc,const TMap<int32,TArray<int32>>& Aliases,double CurvatureRadius)
{
    const int32 Count=I.Vertices.Num();TArray<int32> Rep;Rep.SetNumUninitialized(Count);for(int V=0;V<Count;++V)Rep[V]=V;
    for(const auto& Pair:Aliases)for(int V:Pair.Value)Rep[V]=Pair.Value[0];
    TArray<TSet<int32>> Adj;Adj.SetNum(Count);TMap<uint64,TArray<int32>> Edges;
    TArray<FIntVector> Faces;for(int T=0;T<I.Triangles.Num();T+=3)
    {
        FIntVector F(Rep[I.Triangles[T]],Rep[I.Triangles[T+1]],Rep[I.Triangles[T+2]]);if(F.X==F.Y||F.Y==F.Z||F.Z==F.X)continue;
        const int Face=Faces.Add(F);for(int K=0;K<3;++K){int U=F[K],V=F[(K+1)%3];Adj[U].Add(V);Adj[V].Add(U);const uint64 Key=(uint64(FMath::Min(U,V))<<32)|uint32(FMath::Max(U,V));Edges.FindOrAdd(Key).Add(Face);}
    }
    FDeltaField Base,D;Base.SetNum(Count);D.SetNum(Count);for(int V=0;V<Count;++V){Base[V]=FVector(Skin[V].TransformPosition(I.Vertices[V]));D[V]=FVector(Skin[V].TransformVector(Source[V]+Proc[V]));}
    TArray<FVector> RestNormals;for(const auto& F:Faces)RestNormals.Add(FVector::CrossProduct(Base[F.Y]-Base[F.X],Base[F.Z]-Base[F.X]).GetSafeNormal());
    bool Changed=false;
    for(int Pass=0;Pass<64;++Pass)
    {
        TArray<FVector> Normals;for(const auto& F:Faces)Normals.Add(FVector::CrossProduct(Base[F.Y]+D[F.Y]-Base[F.X]-D[F.X],Base[F.Z]+D[F.Z]-Base[F.X]-D[F.X]).GetSafeNormal());
        TSet<int32> Active;
        for(const auto& Pair:Edges)if(Pair.Value.Num()==2)
        {
            const int A=Pair.Value[0],B=Pair.Value[1];const double RestAngle=FMath::Acos(FMath::Clamp(FVector::DotProduct(RestNormals[A],RestNormals[B]),-1.,1.));
            const auto& FA=Faces[A];const auto& FB=Faces[B];
            const double DualLength=((Base[FA.X]+Base[FA.Y]+Base[FA.Z]-Base[FB.X]-Base[FB.Y]-Base[FB.Z])/3.).Size();
            const double Extra=FMath::Min(5.*PI/180.,DualLength/FMath::Max(.1,CurvatureRadius));
            const double Limit=FMath::Min(PI,FMath::Max(5.*PI/180.,RestAngle+Extra));
            if(FVector::DotProduct(Normals[A],Normals[B])<FMath::Cos(Limit)-1.e-6)for(int F:{A,B})for(int K=0;K<3;++K)Active.Add(Faces[F][K]);
        }
        if(Active.IsEmpty())break;
        auto Next=D;double Maximum=0;
        for(int V:Active)if(!Adj[V].IsEmpty())
        {
            FVector Mean=FVector::ZeroVector;double Total=0;for(int N:Adj[V]){const double W=1/FMath::Max(.01,(Base[V]-Base[N]).Size());Mean+=D[N]*W;Total+=W;}
            Next[V]=FMath::Lerp(D[V],Mean/Total,.3);Maximum=FMath::Max(Maximum,(Next[V]-D[V]).Size());
        }
        if(Maximum<1.e-6)break;Changed=true;D=MoveTemp(Next);
    }
    if(Changed)for(int V=0;V<Count;++V)if(FMath::Abs(Skin[V].Determinant())>=.01)Proc[V]=FVector(Skin[V].Inverse().TransformVector(D[Rep[V]]))-Source[V];
    return Changed;
}
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
    if(I.bInfluencesAreFinalRenderWeights && &Influences==&I.Influences){for(const auto& W:Influences)M[W.Vertex]+=B[W.Bone]*double(W.Weight);return M;}
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
inline bool GrowSafeField(const FVamNativeMeshInput& I,const TArray<FMatrix>& Skin,const FDeltaField& Native,const TMap<int32,TArray<int32>>& Aliases,TArray<double>& Safety,const FDeltaField* Fixed,const TArray<FDeltaField>* Additional=nullptr)
{
    const int Count=Native.Num();const auto Base=Apply(Skin,I.Vertices,true),D=Apply(Skin,Native);
    TArray<FDeltaField> Fields{Native},Displacements{D};if(Additional)for(const auto& Field:*Additional){Fields.Add(Field);Displacements.Add(Apply(Skin,Field));}
    FDeltaField F;F.SetNumZeroed(Count);if(Fixed)F=Apply(Skin,*Fixed);
    TArray<int> Rep;Rep.SetNumUninitialized(Count);for(int V=0;V<Count;++V)Rep[V]=V;
    for(const auto& Pair:Aliases){int R=Count;for(int V:Pair.Value)R=FMath::Min(R,V);for(int V:Pair.Value)Rep[V]=R;}
    TArray<TArray<int>> Faces,Members;Faces.SetNum(Count);Members.SetNum(Count);
    for(int V=0;V<Count;++V)Members[Rep[V]].Add(V);
    for(int T=0;T<I.Triangles.Num();T+=3)for(int K=0;K<3;++K)Faces[Rep[I.Triangles[T+K]]].AddUnique(T);
    TMap<uint64,TArray<int>> EdgeFaces;TArray<TArray<int>> FaceNeighbors;FaceNeighbors.SetNum(I.Triangles.Num()/3);
    TArray<FVector> BaseNormals;BaseNormals.SetNum(FaceNeighbors.Num());
    for(int T=0;T<I.Triangles.Num();T+=3)
    {
        const int A=I.Triangles[T],B=I.Triangles[T+1],C=I.Triangles[T+2];
        BaseNormals[T/3]=FVector::CrossProduct(Base[B]-Base[A],Base[C]-Base[A]).GetSafeNormal();
        for(int K=0;K<3;++K){const int U=Rep[I.Triangles[T+K]],V=Rep[I.Triangles[T+(K+1)%3]];if(U!=V)EdgeFaces.FindOrAdd((uint64(FMath::Min(U,V))<<32)|uint32(FMath::Max(U,V))).Add(T);}
    }
    for(const auto& Pair:EdgeFaces)if(Pair.Value.Num()==2){FaceNeighbors[Pair.Value[0]/3].Add(Pair.Value[1]);FaceNeighbors[Pair.Value[1]/3].Add(Pair.Value[0]);}
    Safety.Init(1,Count);
    auto Normal=[&](int T,int FI){const auto& D=Displacements[FI];const int A=I.Triangles[T],B=I.Triangles[T+1],C=I.Triangles[T+2];const FVector P=Base[A]+F[A]+D[A]*Safety[A];return FVector::CrossProduct(Base[B]+F[B]+D[B]*Safety[B]-P,Base[C]+F[C]+D[C]*Safety[C]-P).GetSafeNormal();};
    bool Verification=true;
    auto SafeFaceFor=[&](int T,int FI)
    {
        const auto& Native=Fields[FI];const auto& D=Displacements[FI];
        const int A=I.Triangles[T],B=I.Triangles[T+1],C=I.Triangles[T+2];
        if(!TriangleSafe(Base[A],Base[B],Base[C],F[A]+D[A]*Safety[A],F[B]+D[B]*Safety[B],F[C]+D[C]*Safety[C],1,(Fixed||Verification)?.05:.051))return false;
        for(int K=0;K<3;++K)
        {
            const int U=I.Triangles[T+K],V=I.Triangles[T+(K+1)%3];
            const double Length=(I.Vertices[U]-I.Vertices[V]).Size();
            const FVector Offset=Fixed?(*Fixed)[U]-(*Fixed)[V]:FVector::ZeroVector;
            if((Offset+Native[U]*Safety[U]-Native[V]*Safety[V]).Size()>=FMath::Max(.15,Length*3)*((Fixed||Verification)?1.:.9999))return false;
            // Limit only the additional differential introduced by attenuation.
            const double Amplitude=FMath::Min(Native[U].Size(),Native[V].Size());
            if(FMath::Abs(Safety[U]-Safety[V])*Amplitude>Length*(Verification?.5:.4999)+1.e-10)return false;
        }
        // Triangle orientation alone permits a severe crease. Check the actual
        // adjoining surface on proven source topology, preserving existing folds.
        // Axis bases are not displayed independently. Keep their original
        // inversion/area/edge checks; bound creases on the composed surface only.
        if(FI!=0)return true;
        const FVector N=Normal(T,FI);
        for(int Other:FaceNeighbors[T/3])
        {
            const double BaseDot=FVector::DotProduct(BaseNormals[T/3],BaseNormals[Other/3]);
            const double Limit=FMath::Min(BaseDot,FMath::Cos(FMath::DegreesToRadians((Fixed||Verification)?30.:29.5)));
            if(FVector::DotProduct(N,Normal(Other,FI))<Limit-((Fixed||Verification)?1.e-6:1.e-8))return false;
        }
        return true;
    };
    auto SafeFace=[&](int T){for(int FI=0;FI<Fields.Num();++FI)if(!SafeFaceFor(T,FI))return false;return true;};
    auto SafeAll=[&](){for(int T=0;T<I.Triangles.Num();T+=3)if(!SafeFace(T))return false;return true;};
    bool Singular=false;for(int V=0;V<Count;++V)if(!Native[V].IsNearlyZero(1.e-8)&&FMath::Abs(Skin[V].Determinant())<.01)Singular=true;
    if(!Singular&&SafeAll())return true;
    Verification=false;
    Safety.Init(0,Count);if(!SafeAll()){UE_LOG(LogTemp,Error,TEXT("GLUTE_GROWTH_FIXED_UNSAFE %s"),*SafetyTraceContext);return false;}
    // Coordinate ascent starts feasible. An accepted update never invalidates
    // a neighboring triangle; no subsequent pass needs to erase its neighbor.
    for(int Pass=0;Pass<12;++Pass)
    {
        double Gain=0;
        for(int Index=0;Index<Count;++Index)
        {
            const int R=Pass%2?Count-1-Index:Index;if(Members[R].IsEmpty()||Safety[R]>.999999999)continue;
            bool Locked=false;for(int V:Members[R])if(!Native[V].IsNearlyZero(1.e-8)&&FMath::Abs(Skin[V].Determinant())<.01)Locked=true;if(Locked)continue;
            const double Start=Safety[R];auto Set=[&](double X){for(int V:Members[R])Safety[V]=X;};
            auto Valid=[&](){for(int T:Faces[R])if(!SafeFace(T))return false;return true;};
            Set(1);if(!Valid())
            {
                double Lo=Start,Hi=1;for(int Step=0;Step<24;++Step){const double Mid=(Lo+Hi)*.5;Set(Mid);if(Valid())Lo=Mid;else Hi=Mid;}
                Set(Lo);
            }
            Gain=FMath::Max(Gain,Safety[R]-Start);
        }
        if(Gain<1.e-7)break;
    }
    // A single vertex can be blocked by curvature while its whole neighborhood
    // can safely recover. Raise proven topology groups together, rechecking every
    // affected constraint before accepting a block (including all shared bases).
    TArray<TArray<int>> Neighbors;Neighbors.SetNum(Count);
    for(int T=0;T<I.Triangles.Num();T+=3)for(int K=0;K<3;++K){int U=Rep[I.Triangles[T+K]],V=Rep[I.Triangles[T+(K+1)%3]];if(U!=V){Neighbors[U].AddUnique(V);Neighbors[V].AddUnique(U);}}
    auto Movable=[&](int R){if(Members[R].IsEmpty()||Safety[R]>.999999999)return false;bool Nonzero=false;for(int V:Members[R])if(!Native[V].IsNearlyZero(1.e-8)){Nonzero=true;if(FMath::Abs(Skin[V].Determinant())<.01)return false;}return Nonzero;};
    auto RaiseBlock=[&](const TArray<int>& Block)
    {
        TArray<int> Affected;TArray<double> Before;for(int R:Block){Before.Add(Safety[R]);for(int T:Faces[R])Affected.AddUnique(T);}
        auto Set=[&](double Amount){for(int J=0;J<Block.Num();++J)for(int V:Members[Block[J]])Safety[V]=Before[J]+(1-Before[J])*Amount;};
        auto Valid=[&](){for(int T:Affected)if(!SafeFace(T))return false;return true;};
        Set(1);double Lo=1;
        if(!Valid()){Lo=0;double Hi=1;for(int Step=0;Step<24;++Step){double Mid=(Lo+Hi)*.5;Set(Mid);if(Valid())Lo=Mid;else Hi=Mid;}Set(Lo);}
        double Gain=0;for(int J=0;J<Block.Num();++J)Gain=FMath::Max(Gain,Safety[Block[J]]-Before[J]);return Gain;
    };
    TSet<int> Visited;
    for(int R=0;R<Count;++R)if(Movable(R)&&!Visited.Contains(R))
    {
        TArray<int> Component{R};Visited.Add(R);
        for(int J=0;J<Component.Num();++J)for(int N:Neighbors[Component[J]])if(Movable(N)&&!Visited.Contains(N)){Visited.Add(N);Component.Add(N);}
        RaiseBlock(Component);
    }
    double BlockGain=0;
    for(int Pass=0;Pass<4;++Pass)
    {
        double Gain=0;
        for(int Index=0;Index<Count;++Index)
        {
            const int R=Pass%2?Count-1-Index:Index;if(!Movable(R))continue;
            TArray<int> Block{R};for(int N:Neighbors[R])if(Movable(N))Block.AddUnique(N);
            const int FirstRing=Block.Num();for(int J=1;J<FirstRing;++J)for(int N:Neighbors[Block[J]])if(Movable(N))Block.AddUnique(N);
            Gain=FMath::Max(Gain,RaiseBlock(Block));
        }
        BlockGain=FMath::Max(BlockGain,Gain);if(Gain<1.e-7)break;
    }
    if(BlockGain>.01)UE_LOG(LogTemp,Display,TEXT("GLUTE_BLOCK_RECOVERY %s maximum_gain=%.9g"),*SafetyTraceContext,BlockGain);
    for(int T=0;T<I.Triangles.Num();T+=3)if(!SafeFace(T)){UE_LOG(LogTemp,Error,TEXT("GLUTE_GROWTH_FINAL_UNSAFE %s triangle=%d,%d,%d"),*SafetyTraceContext,I.Triangles[T],I.Triangles[T+1],I.Triangles[T+2]);return false;}return true;
}
// FixedIsReference protects only the new residual, relative to the supplied
// source/procedural surface. It neither validates nor attenuates that surface.
inline bool Condition(const FVamNativeMeshInput& I,const TArray<FMatrix>& Skin,const FDeltaField& Native,const TMap<int32,TArray<int32>>& Aliases,TArray<double>& Safety,const FDeltaField* Fixed=nullptr,bool FixedIsReference=false)
{
    if(FeasibleSafetyGrowth && !FixedIsReference)return GrowSafeField(I,Skin,Native,Aliases,Safety,Fixed);
    auto Base=Apply(Skin,I.Vertices,true);const auto D=Apply(Skin,Native);Safety.Init(1,Native.Num());for(int32 V=0;V<Native.Num();++V) if(!Native[V].IsNearlyZero(1.e-8) && FMath::Abs(Skin[V].Determinant())<.01) Safety[V]=0;
    FDeltaField F;F.SetNumZeroed(Native.Num());if(Fixed) F=Apply(Skin,*Fixed);
    for(int V=0;V<Native.Num();++V)
    {
        if(Base[V].ContainsNaN() || D[V].ContainsNaN() || F[V].ContainsNaN())return false;
        if(FixedIsReference){Base[V]+=F[V];F[V]=FVector::ZeroVector;}
    }
    for(int32 Pass=0;Pass<80;++Pass)
    {
        auto Next=Safety;bool Good=true;
        for(int32 T=0;T<I.Triangles.Num();T+=3)
        {
            const int32 A=I.Triangles[T],B=I.Triangles[T+1],C=I.Triangles[T+2];const FVector DA=D[A]*Safety[A],DB=D[B]*Safety[B],DC=D[C]*Safety[C];
            auto Safe=[&](double Scale){return TriangleSafe(Base[A],Base[B],Base[C],F[A]+DA*Scale,F[B]+DB*Scale,F[C]+DC*Scale,1,.05);};
            if(!Safe(1))
            {
                if((SafetyTraceVertices.Contains(A)||SafetyTraceVertices.Contains(B)||SafetyTraceVertices.Contains(C)))
                {
                    const FVector N0=FVector::CrossProduct(Base[B]-Base[A],Base[C]-Base[A]);
                    const FVector N1=FVector::CrossProduct(Base[B]+F[B]+DB-Base[A]-F[A]-DA,Base[C]+F[C]+DC-Base[A]-F[A]-DA);
                    UE_LOG(LogTemp,Display,TEXT("GLUTE_SAFETY_CAUSE %s condition_pass=%d triangle %d %d %d cosine=%.9g area_ratio=%.9g"),*SafetyTraceContext,Pass,A,B,C,FVector::DotProduct(N0.GetSafeNormal(),N1.GetSafeNormal()),N1.Size()/FMath::Max(1.e-20,N0.Size()));
                }
                if(Fixed && !Safe(0)) return false;
                Good=false;double Lo=0,Hi=1;for(int32 N=0;N<30;++N) { const double M=(Lo+Hi)*.5;if(Safe(M)) Lo=M;else Hi=M; }
                for(int32 V:{A,B,C}) Next[V]=FMath::Min(Next[V],Safety[V]*Lo*.99);
            }
            for(int32 K=0;K<3;++K)
            {
                const int32 U=I.Triangles[T+K],V=I.Triangles[T+(K+1)%3];const double Limit=FMath::Max(.15,(I.Vertices[U]-I.Vertices[V]).Size()*3);const FVector Offset=Fixed && !FixedIsReference?(*Fixed)[U]-(*Fixed)[V]:FVector::ZeroVector,Adjust=Native[U]*Safety[U]-Native[V]*Safety[V];
                if((Offset+Adjust).Size()>=Limit) { if((SafetyTraceVertices.Contains(U)||SafetyTraceVertices.Contains(V)))UE_LOG(LogTemp,Display,TEXT("GLUTE_SAFETY_CAUSE %s condition_pass=%d native_edge %d %d length=%.9g limit=%.9g"),*SafetyTraceContext,Pass,U,V,(Offset+Adjust).Size(),Limit);Good=false;double Lo=0,Hi=1;if(Offset.Size()>=Limit) return false;for(int32 N=0;N<30;++N) { const double M=(Lo+Hi)*.5;if((Offset+Adjust*M).Size()<Limit) Lo=M;else Hi=M; }const double Factor=Lo*.99;Next[U]=FMath::Min(Next[U],Safety[U]*Factor);Next[V]=FMath::Min(Next[V],Safety[V]*Factor); }
            }
        }
        if(Good) return true;
        for(const auto& Pair:Aliases) { double M=1;for(int32 V:Pair.Value) M=FMath::Min(M,Next[V]);for(int32 V:Pair.Value) Next[V]=M; }
        Safety=MoveTemp(Next);
    }return false;
}
}

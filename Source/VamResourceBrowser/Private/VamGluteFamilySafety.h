#pragma once
#include "VamGluteRotationBlend.h"
#include "VamGluteCorrectiveProfile.h"
#include "Async/ParallelFor.h"
#include <atomic>

namespace VamGluteGeometry
{
inline bool JointFamilySafety=false;
// All pose states remain feasible after every accepted update. A later pose
// therefore cannot erase an earlier pose's recovery. This is a bake-time solver.
struct FGluteJointSafety
{
    struct FPose { TArray<FMatrix> Skin;FDeltaField Base,Normals; };
    struct FScene
    {
        TSharedPtr<FPose> Pose;TArray<double> Weights;int AxisTarget=INDEX_NONE;
        FDeltaField AxisGoal,Fixed,FixedDelta,Native,Delta,GoalNormals,FixedNormals;TArray<double> Margins;
        bool HasFixed=false,Continuity=true;
    };
    struct FVariable { int Target,Root;double Before; };
    struct FTrialScene { bool Active=false;FDeltaField Before,BeforeDelta,Move,MoveDelta; };
    const FVamNativeMeshInput& Input;const TArray<FDeltaField>& Goals;
    TArray<int> Rep;TArray<TArray<int>> Members,Neighbors,Faces,FaceNeighbors;
    TArray<TArray<double>> Alpha;TArray<FScene> Scenes;
    FGluteJointSafety(const FVamNativeMeshInput& I,const TArray<FDeltaField>& InGoals,const TMap<int32,TArray<int32>>& Aliases):Input(I),Goals(InGoals)
    {
        const int Count=I.Vertices.Num();Rep.SetNum(Count);Members.SetNum(Count);Neighbors.SetNum(Count);Faces.SetNum(Count);
        for(int V=0;V<Count;++V)Rep[V]=V;
        for(const auto& Pair:Aliases){int R=Count;for(int V:Pair.Value)R=FMath::Min(R,V);for(int V:Pair.Value)Rep[V]=R;}
        for(int V=0;V<Count;++V)Members[Rep[V]].Add(V);
        FaceNeighbors.SetNum(I.Triangles.Num()/3);TMap<uint64,TArray<int>> Edges;
        for(int T=0;T<I.Triangles.Num();T+=3)for(int K=0;K<3;++K)
        {
            const int U=Rep[I.Triangles[T+K]],V=Rep[I.Triangles[T+(K+1)%3]];Faces[U].AddUnique(T/3);
            if(U!=V){Neighbors[U].AddUnique(V);Neighbors[V].AddUnique(U);Edges.FindOrAdd((uint64(FMath::Min(U,V))<<32)|uint32(FMath::Max(U,V))).AddUnique(T/3);}
        }
        for(const auto& E:Edges)if(E.Value.Num()==2){FaceNeighbors[E.Value[0]].Add(E.Value[1]);FaceNeighbors[E.Value[1]].Add(E.Value[0]);}
        Alpha.SetNum(Goals.Num());for(int K=0;K<Goals.Num();++K){Alpha[K].Init(0,Count);for(int V=0;V<Count;++V)if(Goals[K][V].IsNearlyZero(1.e-12))Alpha[K][V]=1;}
    }
    FVector Normal(const FScene& S,int Face,const FDeltaField& Delta)const
    {
        const int T=Face*3,A=Input.Triangles[T],B=Input.Triangles[T+1],C=Input.Triangles[T+2];
        return FVector::CrossProduct(S.Pose->Base[B]+Delta[B]-S.Pose->Base[A]-Delta[A],S.Pose->Base[C]+Delta[C]-S.Pose->Base[A]-Delta[A]).GetSafeNormal();
    }
    void AddScene(TSharedPtr<FPose> Pose,const TArray<double>& Weights,const FDeltaField& Fixed,bool HasFixed,bool Continuity,int AxisTarget=INDEX_NONE,const FDeltaField& AxisGoal=FDeltaField())
    {
        auto& S=Scenes.AddDefaulted_GetRef();S.Pose=MoveTemp(Pose);S.Weights=Weights;S.Fixed=Fixed;S.HasFixed=HasFixed;S.Continuity=Continuity;S.AxisTarget=AxisTarget;S.AxisGoal=AxisGoal;
        S.Native=Fixed;S.FixedDelta=Apply(S.Pose->Skin,Fixed);S.Delta=S.FixedDelta;S.Margins.Init(.051,Input.Triangles.Num()/3);
        // A continuity guard must distinguish new attenuation creases from the
        // curvature already present in the requested source deformation. Using
        // only the uncorrected surface also pins infinitesimal RBF tails at an
        // existing crease and propagates that restriction into other poses.
        FDeltaField GoalNative=Fixed;
        for(int V=0;V<Input.Vertices.Num();++V)
            if(AxisTarget>=0)GoalNative[V]+=AxisGoal[V];
            else for(int K=0;K<Goals.Num();++K)GoalNative[V]+=Goals[K][V]*Weights[K];
        const auto GoalDelta=Apply(S.Pose->Skin,GoalNative);
        for(int F=0;F<FaceNeighbors.Num();++F){S.GoalNormals.Add(Normal(S,F,GoalDelta));S.FixedNormals.Add(Normal(S,F,S.FixedDelta));}

        if(HasFixed)for(int F=0;F<S.Margins.Num();++F)
        {
            const int T=F*3,A=Input.Triangles[T],B=Input.Triangles[T+1],C=Input.Triangles[T+2];const auto& P=S.Pose->Base;
            const FVector N0=FVector::CrossProduct(P[B]-P[A],P[C]-P[A]),NF=FVector::CrossProduct(P[B]+S.Delta[B]-P[A]-S.Delta[A],P[C]+S.Delta[C]-P[A]-S.Delta[A]);
            const double Area=1-NF.Size()/FMath::Max(.01,N0.Size()*5),Signed=N0.SizeSquared()<1.e-16?1:FVector::DotProduct(N0,NF)/N0.SizeSquared();
            S.Margins[F]=FMath::Clamp(.05+(FMath::Min(Area,Signed)-.05)*.5,.05,.051);
        }
    }
    bool ValidScene(const FScene& S,const TArray<int>& CheckFaces,const TArray<int>& Vertices,bool Strict)const
    {
        for(int V:Vertices)if(S.Native[V].ContainsNaN() || (!S.Native[V].IsNearlyZero(1.e-8)&&FMath::Abs(S.Pose->Skin[V].Determinant())<.01))return false;
        const auto& P=S.Pose->Base;
        for(int Face:CheckFaces)
        {
            const int T=Face*3,A=Input.Triangles[T],B=Input.Triangles[T+1],C=Input.Triangles[T+2];
            if(!TriangleSafe(P[A],P[B],P[C],S.Delta[A],S.Delta[B],S.Delta[C],1,Strict?S.Margins[Face]:.05))return false;
            for(int K=0;K<3;++K)
            {
                const int U=Input.Triangles[T+K],V=Input.Triangles[T+(K+1)%3];const double Limit=FMath::Max(.15,(Input.Vertices[U]-Input.Vertices[V]).Size()*3);
                const double Reserve=Strict?FMath::Min(Limit*.0001,FMath::Max(0.,Limit-(S.Fixed[U]-S.Fixed[V]).Size())*.5):0;
                if((S.Native[U]-S.Native[V]).Size()>=Limit-Reserve)return false;
            }
            if(S.Continuity)
            {
                const FVector N=Normal(S,Face,S.Delta);
                for(int Other:FaceNeighbors[Face])
                {
                    const double InputDot=FVector::DotProduct(S.FixedNormals[Face],S.FixedNormals[Other]);const double BaseDot=S.HasFixed?InputDot:FMath::Min(InputDot,FVector::DotProduct(S.GoalNormals[Face],S.GoalNormals[Other]));
                    const double Relaxed=FMath::Min(BaseDot,FMath::Cos(FMath::DegreesToRadians(30.)))-1.e-6;
                    double Limit=Strict?FMath::Min(BaseDot,FMath::Cos(FMath::DegreesToRadians(29.5)))-1.e-8:Relaxed;
                    if(Strict&&S.HasFixed)Limit=FMath::Min(Limit,(FVector::DotProduct(S.FixedNormals[Face],S.FixedNormals[Other])+Relaxed)*.5);
                    if(FVector::DotProduct(N,Normal(S,Other,S.Delta))<Limit)return false;
                }
            }
        }
        return true;
    }
    bool AllValid(bool Strict)const
    {
        TArray<int> AllFaces,AllVertices;for(int F=0;F<FaceNeighbors.Num();++F)AllFaces.Add(F);for(int V=0;V<Input.Vertices.Num();++V)AllVertices.Add(V);
        std::atomic<bool> Good{true};ParallelFor(Scenes.Num(),[&](int C){if(!ValidScene(Scenes[C],AllFaces,AllVertices,Strict))Good.store(false,std::memory_order_relaxed);});return Good.load();
    }
    bool Movable(int K,int R)const{return !Members[R].IsEmpty()&&Alpha[K][R]<.999999999&&!Goals[K][R].IsNearlyZero(1.e-8);}
    double Raise(const TArray<int>& Roots,int OnlyTarget)
    {
        TArray<FVariable> Variables;for(int R:Roots)for(int K=0;K<Goals.Num();++K)if((OnlyTarget<0||OnlyTarget==K)&&Movable(K,R))Variables.Add({K,R,Alpha[K][R]});
        if(Variables.IsEmpty())return 0;
        TArray<int> Vertices,CheckFaces,Slots;Slots.Init(INDEX_NONE,Input.Vertices.Num());TSet<int> FaceSet;
        for(const auto& V:Variables){for(int P:Members[V.Root])if(Slots[P]==INDEX_NONE){Slots[P]=Vertices.Num();Vertices.Add(P);}for(int F:Faces[V.Root])FaceSet.Add(F);}
        CheckFaces=FaceSet.Array();CheckFaces.Sort();TArray<FTrialScene> Trial;Trial.SetNum(Scenes.Num());
        ParallelFor(Scenes.Num(),[&](int C)
        {
            const auto& S=Scenes[C];auto& Q=Trial[C];Q.Move.Init(FVector::ZeroVector,Vertices.Num());
            for(const auto& V:Variables)
            {
                const double W=S.AxisTarget>=0?(S.AxisTarget==V.Target?1.:0.):S.Weights[V.Target];if(W==0)continue;
                for(int P:Members[V.Root])Q.Move[Slots[P]]+=(S.AxisTarget>=0?S.AxisGoal[P]:Goals[V.Target][P])*(W*(1-V.Before));
            }
            for(const auto& D:Q.Move)if(!D.IsNearlyZero(1.e-16)){Q.Active=true;break;}
            if(!Q.Active)return;
            for(int J=0;J<Vertices.Num();++J){const int P=Vertices[J];Q.Before.Add(S.Native[P]);Q.BeforeDelta.Add(S.Delta[P]);Q.MoveDelta.Add(FVector(S.Pose->Skin[P].TransformVector(Q.Move[J])));}
        });
        auto Test=[&](double Amount)
        {
            for(const auto& V:Variables)for(int P:Members[V.Root])Alpha[V.Target][P]=V.Before+(1-V.Before)*Amount;
            for(const auto& V:Variables)for(int N:Neighbors[V.Root])
            {
                const double Amplitude=FMath::Min(Goals[V.Target][V.Root].Size(),Goals[V.Target][N].Size());
                if(FMath::Abs(Alpha[V.Target][V.Root]-Alpha[V.Target][N])*Amplitude>(Input.Vertices[V.Root]-Input.Vertices[N]).Size()*.4999+1.e-10)return false;
            }
            std::atomic<bool> Good{true};ParallelFor(Scenes.Num(),[&](int C)
            {
                const auto& Q=Trial[C];if(!Q.Active)return;auto& S=Scenes[C];
                for(int J=0;J<Vertices.Num();++J){const int P=Vertices[J];S.Native[P]=Q.Before[J]+Q.Move[J]*Amount;S.Delta[P]=Q.BeforeDelta[J]+Q.MoveDelta[J]*Amount;}
                if(!ValidScene(S,CheckFaces,Vertices,true))Good.store(false,std::memory_order_relaxed);
            });return Good.load();
        };
        double Lo=1;if(!Test(1)){Lo=0;double Hi=1;for(int Step=0;Step<20;++Step){const double M=(Lo+Hi)*.5;if(Test(M))Lo=M;else Hi=M;}if(!Test(Lo)){UE_LOG(LogTemp,Error,TEXT("GLUTE_JOINT_ACCEPTANCE_LOST"));return -1;}}
        double Gain=0;for(const auto& V:Variables)Gain=FMath::Max(Gain,Alpha[V.Target][V.Root]-V.Before);return Gain;
    }
    bool Solve()
    {
        for(const auto& Field:Goals)for(const auto& V:Field)if(V.ContainsNaN())return false;
        if(!AllValid(false)){UE_LOG(LogTemp,Error,TEXT("GLUTE_JOINT_FIXED_UNSAFE"));return false;}
        TArray<int> AllRoots;for(int R=0;R<Members.Num();++R)if(!Members[R].IsEmpty())AllRoots.Add(R);
        if(Raise(AllRoots,INDEX_NONE)<0)return false;
        for(int Pass=0;Pass<16;++Pass)
        {
            double Gain=0;int Updates=0;
            for(int Depth : {12,6,3,1})
            for(int K=INDEX_NONE;K<Goals.Num();++K)
            {
                TSet<int> Visited;
                for(int I=0;I<AllRoots.Num();++I)
                {
                    const int R=AllRoots[Pass%2?AllRoots.Num()-1-I:I];if(Visited.Contains(R))continue;
                    bool Active=false;for(int J=0;J<Goals.Num();++J)if((K<0||J==K)&&Movable(J,R)){Active=true;break;}if(!Active)continue;
                    TArray<int> Patch{R};TSet<int> Seen{R};int Begin=0;for(int Ring=0;Ring<Depth;++Ring){const int End=Patch.Num();for(int J=Begin;J<End;++J)for(int N:Neighbors[Patch[J]])if(!Seen.Contains(N)){Seen.Add(N);Patch.Add(N);}Begin=End;}for(int N:Patch)Visited.Add(N);
                    const double G=Raise(Patch,K);if(G<0)return false;Gain=FMath::Max(Gain,G);if(G>1.e-7)++Updates;
                }
            }
            UE_LOG(LogTemp,Display,TEXT("GLUTE_JOINT_RECOVERY pass=%d updates=%d maximum_gain=%.9g scenes=%d"),Pass,Updates,Gain,Scenes.Num());if(Gain<1.e-6)break;
        }
        // Validate the exact field reconstruction that the Morph builder will use,
        // rather than only the incrementally accumulated optimizer state.
        for(auto& S:Scenes)
        {
            S.Native=S.Fixed;
            for(int V=0;V<Input.Vertices.Num();++V)
                if(S.AxisTarget>=0)S.Native[V]+=S.AxisGoal[V]*Alpha[S.AxisTarget][V];
                else for(int K=0;K<Goals.Num();++K)if(S.Weights[K]!=0)S.Native[V]+=Goals[K][V]*(S.Weights[K]*Alpha[K][V]);
            S.Delta=Apply(S.Pose->Skin,S.Native);
        }
        if(!AllValid(false)){UE_LOG(LogTemp,Error,TEXT("GLUTE_JOINT_FINAL_UNSAFE"));return false;}return true;
    }
};

inline bool ConditionTargetFamily(const FVamNativeMeshInput& I,const UVamGluteStructureProfile& G,const UVamGluteCorrectiveProfile& P,const TArray<FTransform>& Bind,const TMap<int32,TArray<int32>>& Aliases,const TArray<TArray<FDeltaField>>& Goal,const TArray<TArray<FDeltaField>>* Fixed,TArray<TArray<TArray<double>>>& Factors,TFunctionRef<FVector(int)> Probe)
{
    const int Targets=P.Targets.Num()-1,Count=I.Vertices.Num();TArray<FDeltaField> Flat,FixedFlat;
    for(int S=0;S<2;++S)for(int T=1;T<=Targets;++T){Flat.Add(Goal[S][T]);if(Fixed)FixedFlat.Add((*Fixed)[S][T]);}
    FGluteJointSafety Solver(I,Flat,Aliases);
    struct FStoredPose { FVector L,R;TSharedPtr<FGluteJointSafety::FPose> Data; };TArray<FStoredPose> Cache;
    auto GetPose=[&](const FVector& L,const FVector& R) -> TSharedPtr<FGluteJointSafety::FPose>
    {
        for(const auto& E:Cache)if(E.L==L&&E.R==R)return E.Data;
        auto D=MakeShared<FGluteJointSafety::FPose>();D->Skin=Skin(I,I.Influences,Bind,Pose(I,G,Bind,L,R,true));D->Base=Apply(D->Skin,I.Vertices,true);
        for(int T=0;T<I.Triangles.Num();T+=3)D->Normals.Add(FVector::CrossProduct(D->Base[I.Triangles[T+1]]-D->Base[I.Triangles[T]],D->Base[I.Triangles[T+2]]-D->Base[I.Triangles[T]]).GetSafeNormal());
        Cache.Add({L,R,D});return D;
    };
    auto Add=[&](TSharedPtr<FGluteJointSafety::FPose> M,const TArray<double>& W,bool Continuity,int AxisTarget=INDEX_NONE,int Axis=0,int Side=0)
    {
        FDeltaField F,A;F.Init(FVector::ZeroVector,Count);
        if(AxisTarget>=0)
        {
            A=F;const FTransform Anchor=G.Sides[Side].AnchorLocal*Bind[G.Sides[Side].PelvisBone];
            for(int V=0;V<Count;++V)if(FMath::Abs(M->Skin[V].Determinant())>=.01)
            {
                auto Project=[&](const FVector& N){const FVector Local=Anchor.InverseTransformVectorNoScale(FVector(M->Skin[V].TransformVector(N)));FVector D=FVector::ZeroVector;D[Axis]=Local[Axis];return FVector(M->Skin[V].Inverse().TransformVector(Anchor.TransformVectorNoScale(D)));};
                A[V]=Project(Flat[AxisTarget][V]);if(Fixed)F[V]=Project(FixedFlat[AxisTarget][V]);
            }
        }
        else if(Fixed)for(int K=0;K<Flat.Num();++K)if(W[K]!=0)for(int V=0;V<Count;++V)F[V]+=FixedFlat[K][V]*W[K];
        Solver.AddScene(M,W,F,Fixed!=nullptr,Continuity,AxisTarget,A);
    };
    for(int S=0;S<2;++S)for(int T=1;T<=Targets;++T)
    {
        const auto D=P.Targets[T].Degrees;auto M=GetPose(S==0?D:FVector::ZeroVector,S==1?D:FVector::ZeroVector);TArray<double> W;W.Init(0,Flat.Num());const int K=S*Targets+T-1;W[K]=1;Add(M,W,true);for(int Axis=0;Axis<3;++Axis)Add(M,W,false,K,Axis,S);
    }
    for(int Side=0;Side<3;++Side)for(int Step=0;Step<72;++Step)
    {
        const FVector D=Probe(Step);FVamHipSidePose H;H.FlexionExtension=D.X*PI/180.;H.AbductionAdduction=D.Y*PI/180.;H.ExternalInternalRotation=D.Z*PI/180.;const auto W0=VamGluteCorrective::Weights(P,H);TArray<double> W;W.Init(0,Flat.Num());
        for(int S=0;S<2;++S)if(Side==2||Side==S)for(int T=1;T<=Targets;++T)W[S*Targets+T-1]=W0[T];
        // Keep all original geometric checks on extrapolation probes. Additional
        // aesthetic continuity is evaluated on authored/interior poses, not on
        // the -40/130 degree probes as if they were measured anatomical targets.
        Add(GetPose(Side!=1?D:FVector::ZeroVector,Side!=0?D:FVector::ZeroVector),W,Step<54);
    }
    // Diagnose inverse-conditioning restrictions separately from forward output
    // geometry. Read-only: this never bypasses an active safety predicate.
    for(int C=0;C<Solver.Scenes.Num();++C)
    {
        const auto& S=Solver.Scenes[C];int Locked=0;
        for(int V=0;V<Count;++V)
        {
            FVector N=S.Fixed[V];
            if(S.AxisTarget>=0)N+=S.AxisGoal[V];
            else for(int K=0;K<Flat.Num();++K)N+=Flat[K][V]*S.Weights[K];
            const double Determinant=FMath::Abs(S.Pose->Skin[V].Determinant());
            if(!N.IsNearlyZero(1.e-8)&&Determinant<.01)
            {
                if(Locked<8)UE_LOG(LogTemp,Display,TEXT("GLUTE_JOINT_INVERSE_LOCK fixed=%d scene=%d axis_target=%d input=%d determinant=%.9g native_cm=%.9g posed_base=%s"),Fixed!=nullptr,C,S.AxisTarget,V,Determinant,N.Size(),*S.Pose->Base[V].ToString());
                ++Locked;
            }
        }
        if(Locked)UE_LOG(LogTemp,Display,TEXT("GLUTE_JOINT_INVERSE_LOCK_COUNT fixed=%d scene=%d count=%d"),Fixed!=nullptr,C,Locked);
    }

    UE_LOG(LogTemp,Display,TEXT("GLUTE_JOINT_BEGIN fixed=%d scenes=%d poses=%d"),Fixed!=nullptr,Solver.Scenes.Num(),Cache.Num());
    if(!Solver.Solve())return false;
    Factors.SetNum(2);for(int S=0;S<2;++S){Factors[S].SetNum(Targets+1);for(int T=1;T<=Targets;++T)Factors[S][T]=MoveTemp(Solver.Alpha[S*Targets+T-1]);}return true;
}
}

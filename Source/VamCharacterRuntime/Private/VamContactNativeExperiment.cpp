#include "VamContactNativeExperiment.h"
#include "Chaos/Deformable/GaussSeidelMainConstraint.h"
#include "Chaos/Deformable/GaussSeidelWeakConstraints.h"
#include "GeometryCollection/Facades/CollectionPositionTargetFacade.h"
#include "GeometryCollection/Facades/CollectionVolumeConstraintFacade.h"
#include "GeometryCollection/GeometryCollection.h"
#include "HAL/IConsoleManager.h"
namespace
{
TAutoConsoleVariable<int32> CVarContactMaterialCache(TEXT("vam.Contact.MaterialCache"),0,TEXT("0 native calculation, 1 static geometry, 2 exact deformation-gradient stress memoization. Reset to apply."));
TAutoConsoleVariable<int32> CVarContactPolar(TEXT("vam.Contact.ExperimentalPolar"),0,TEXT("Experimental Newton polar with native SVD fallback; requires GS adapter, reset to apply."));
class FContactMaterialExperiment : public Chaos::Softs::FGaussSeidelCorotatedTetrahedralConstraints
{
public:
    using FGaussSeidelCorotatedTetrahedralConstraints::FGaussSeidelCorotatedTetrahedralConstraints;
    using M=Chaos::PMatrix<float,3,3>;
    using V=Chaos::TVec3<float>;
    using XPBD=Chaos::Softs::FXPBDCorotatedTetrahedralConstraints;
    struct FCellCache { M WeightedInverseTranspose; M LastF; M Stress; float Mu=0,Lambda=0,Volume=0; bool Valid=false; uint64 Hits=0,Calls=0; };
    TArray<FCellCache> Cells;
    int CacheMode=0;
    void PrepareCache(int Mode)
    {
        CacheMode=Mode;if(!Mode)return;
        Cells.SetNum(XPBD::Measure.Num());
        for(int E=0;E<Cells.Num();++E)
        {
            auto& C=Cells[E];C.Volume=XPBD::Measure[E];C.Mu=XPBD::MuElementArray[E];C.Lambda=XPBD::LambdaElementArray[E];
            C.WeightedInverseTranspose=-C.Volume*XPBD::ElementDmInv(E).GetTransposed();
        }
        UE_LOG(LogTemp,Display,TEXT("CONTACT_MATERIAL_CACHE mode=%d bytes=%lld"),Mode,int64(Cells.GetAllocatedSize()));
    }
    virtual ~FContactMaterialExperiment() override
    {
        if(CacheMode==2){uint64 Hits=0,Calls=0;for(const auto& C:Cells){Hits+=C.Hits;Calls+=C.Calls;}
            UE_LOG(LogTemp,Display,TEXT("CONTACT_MATERIAL_CACHE hits=%llu calls=%llu"),Hits,Calls);}
    }
    void AddCached(const Chaos::Softs::FSolverParticlesRange& Particles,int E,int Local,float Dt,V& Residual,M& Hessian)
    {
        if(!CacheMode){AddHyperelasticResidualAndHessian(Particles,E,Local,Dt,Residual,Hessian);return;}
        auto& C=Cells[E];M Ds(0.f);
        const auto& Tet=XPBD::MeshConstraints[E];
        for(int I=0;I<3;++I)for(int Col=0;Col<3;++Col)Ds.SetAt(Col,I,Particles.GetP(Tet[I+1])[Col]-Particles.GetP(Tet[0])[Col]);
        const M Fe=XPBD::ElementDmInv(E)*Ds;M Stress(0.f);
        // Native static graph coloring prevents two vertices of a tet from
        // executing concurrently. Cache is per material/evolution, not shared.
        bool Hit=CacheMode==2&&C.Valid;
        if(Hit)for(int A=0;A<3;++A)for(int B=0;B<3;++B)Hit &= Fe.GetAt(A,B)==C.LastF.GetAt(A,B);
        if(Hit){Stress=C.Stress;++C.Hits;}
        else {ComputeStress(Fe,C.Mu,C.Lambda,Stress);if(CacheMode==2){C.LastF=Fe;C.Stress=Stress;C.Valid=true;}}
        if(CacheMode==2)++C.Calls;
        const M Force=C.WeightedInverseTranspose*Stress;V Dx(0.f);
        if(Local>0){for(int A=0;A<3;++A)Dx[A]+=Force.GetAt(A,Local-1);}
        else {for(int A=0;A<3;++A)for(int Col=0;Col<3;++Col)Dx[A]-=Force.GetAt(A,Col);}
        Dx*=Dt*Dt;for(int A=0;A<3;++A)Residual[A]-=Dx[A];
        ComputeHessianHelper(Fe,XPBD::ElementDmInv(E),C.Mu,C.Lambda,Local,Dt*Dt*C.Volume,Hessian);
    }
    void EnablePolarExperiment()
    {
        auto Native=ComputeStress;
        ComputeStress=[Native](const Chaos::PMatrix<float,3,3>& F,float InMu,float InLambda,Chaos::PMatrix<float,3,3>& P)
        {
            using M=Chaos::PMatrix<float,3,3>;
            const float J=F.Determinant();
            if(!FMath::IsFinite(J)||J<.1f||J>10.f){Native(F,InMu,InLambda,P);return;}
            M R=F;bool Converged=false;
            for(int It=0;It<8;++It)
            {
                const float D=R.Determinant();
                if(!FMath::IsFinite(D)||FMath::Abs(D)<1.e-8f)break;
                const M Next=(R+R.Inverse().GetTransposed())*.5f;
                float Error=0;for(int A=0;A<3;++A)for(int B=0;B<3;++B)Error+=FMath::Square(Next.GetAt(A,B)-R.GetAt(A,B));
                R=Next;if(Error<1.e-12f){Converged=true;break;}
            }
            if(!Converged){Native(F,InMu,InLambda,P);return;}
            // Positive-det, converged orthogonal factor only. Singular or inverted
            // elements retain the engine's robust SVD path. No temporal cache.
            const M Gram=R.GetTransposed()*R;
            float Error=0;for(int A=0;A<3;++A)for(int B=0;B<3;++B)Error+=FMath::Square(Gram.GetAt(A,B)-(A==B?1.f:0.f));
            if(!FMath::IsFinite(Error)||Error>1.e-10f){Native(F,InMu,InLambda,P);return;}
            const M Cofactor=F.Inverse().GetTransposed()*J;
            P=2.f*InMu*(F-R)+InLambda*(J-1.f)*Cofactor;
        };
    }
};
}

// Private single-proxy breast solver adapter; preserves native material, color
// order and attachments. Unknown layouts leave the original rule untouched.
// All captured mutable constraints belong to this evolution, never a shared asset.
bool VamInstallContactNativeExperiment(Chaos::Softs::FPBDEvolution& E,
    Chaos::Softs::FFleshThreadingProxy& Proxy,
    const Chaos::Softs::FDeformableSolverProperties& Properties, int32 Batch)
{
    using namespace Chaos::Softs;
    using Vec=Chaos::TVec3<FSolverReal>;
    using Mat=Chaos::PMatrix<FSolverReal,3,3>;
    const auto& Rest=Proxy.GetRestCollection();const auto Range=Proxy.GetSolverParticleRange();
    const auto* Tets=Rest.FindAttributeTyped<FIntVector4>(TEXT("Tetrahedron"),TEXT("Tetrahedral"));
    const auto* Young=Rest.FindAttributeTyped<float>(TEXT("Stiffness"),FGeometryCollection::VerticesGroup);
    const auto* Nu=Rest.FindAttributeTyped<float>(TEXT("Incompressibility"),FGeometryCollection::VerticesGroup);
    const auto* Alpha=Rest.FindAttributeTyped<float>(TEXT("Inflation"),FGeometryCollection::VerticesGroup);
    if(!Tets||!Young||!Nu||!Alpha||Range.Start!=0||E.ConstraintRules().Num()!=1||
       !Properties.bDoQuasistatics||Properties.bUseGSNeohookean||!Properties.bEnablePositionTargets||
       Properties.bUseGridBasedConstraints||Properties.bDoSpringCollision||Properties.bDoSphereRepulsion||
       Properties.bDoLengthBasedMuscleActivation||Properties.bOverrideMuscleActivationWithAnimatedCurves)return false;
    if(GeometryCollection::Facades::FVolumeConstraintFacade(Rest).NumVolumeConstraints()!=0)return false;
    const int Count=E.Particles().Size();
    if(Young->Num()!=Count||Nu->Num()!=Count||Alpha->Num()!=Count)return false;
    TArray<Chaos::TVector<int32,4>> Mesh;TArray<FSolverReal> Ys,Ns,As;
    TArray<TArray<int32>> Incident,Local;Incident.SetNum(Count);Local.SetNum(Count);
    for(int K=0;K<Tets->Num();++K)
    {
        const auto T=(*Tets)[K];
        for(int J=0;J<4;++J)if(T[J]<0||T[J]>=Count)return false;
        Mesh.Add(Chaos::TVector<int32,4>(T[0],T[1],T[2],T[3]));
        Ys.Add(((*Young)[T[0]]+(*Young)[T[1]]+(*Young)[T[2]]+(*Young)[T[3]])/4.f);
        Ns.Add(((*Nu)[T[0]]+(*Nu)[T[1]]+(*Nu)[T[2]]+(*Nu)[T[3]])/4.f);
        As.Add(((*Alpha)[T[0]]+(*Alpha)[T[1]]+(*Alpha)[T[2]]+(*Alpha)[T[3]])/4.f);
        for(int J=0;J<4;++J){Incident[T[J]].Add(K);Local[T[J]].Add(J);}
    }
    const auto* OriginalIncident=Rest.FindAttributeTyped<TArray<int32>>(TEXT("IncidentElements"),TEXT("Vertices"));
    const auto* OriginalLocal=Rest.FindAttributeTyped<TArray<int32>>(TEXT("IncidentElementsLocalIndex"),TEXT("Vertices"));
    const auto* Vertices=Rest.FindAttributeTyped<FVector3f>(TEXT("Vertex"),TEXT("Vertices"));
    if(!OriginalIncident||!OriginalLocal||!Vertices||Vertices->Num()!=Count||OriginalIncident->Num()!=Count||OriginalLocal->Num()!=Count)return false;
    FSolverParticles RestParticles;RestParticles.AddParticles(Count);
    for(int I=0;I<Count;++I)
    {
        Incident[I]=(*OriginalIncident)[I];Local[I]=(*OriginalLocal)[I];
        RestParticles.X(I)=Vec(Proxy.GetInitialPointsTransform().TransformPosition(FVector((*Vertices)[I])));
        RestParticles.P(I)=RestParticles.X(I);RestParticles.V(I)=Vec(0);
        RestParticles.M(I)=E.Particles().M(I);RestParticles.InvM(I)=E.Particles().InvM(I);
    }
    auto Material=MakeShared<FContactMaterialExperiment>(RestParticles,Mesh,Ys,Ns,MoveTemp(As),MoveTemp(Incident),MoveTemp(Local),0,Count,true,Properties.bUseSOR,Properties.OmegaSOR);
    Material->PrepareCache(FMath::Clamp(CVarContactMaterialCache.GetValueOnGameThread(),0,2));
    if(CVarContactPolar.GetValueOnGameThread()!=0)Material->EnablePolarExperiment();
    FDeformableXPBDCorotatedParams Params;Params.XPBDCorotatedBatchSize=FMath::Clamp(Batch,1,512);
    const auto* Parallel=IConsoleManager::Get().FindConsoleVariable(TEXT("p.Chaos.Deformable.GSParallelMax"));
    const auto* MaxDx=IConsoleManager::Get().FindConsoleVariable(TEXT("p.Chaos.Deformable.GSMaxDxRatio"));
    auto Main=MakeShared<FGaussSeidelMainConstraints>(RestParticles,true,Properties.bUseSOR,Properties.OmegaSOR,Parallel?Parallel->GetInt():100,MaxDx?MaxDx->GetFloat():1.f,Params);
    Main->AddStaticConstraints(Material->GetMeshArray(),Material->GetIncidentElements(),Material->GetIncidentElementsLocal());
    int Index=Main->AddStaticConstraintResidualAndHessianRange(1);
    Main->StaticConstraintResidualAndHessian()[Index]=[Material](const FSolverParticlesRange& P,int T,int L,FSolverReal Dt,Vec& R,Mat& H)
    {Material->AddCached(P,T,L,Dt,R,H);};
    GeometryCollection::Facades::FPositionTargetFacade Targets(Rest);
    TArray<TArray<int32>> Source,Target;TArray<TArray<FSolverReal>> SW,TW;TArray<FSolverReal> Stiffness;TArray<bool> Aniso,Zero;
    for(int K=0;K<Targets.NumPositionTargets();++K)
    {
        const auto T=Targets.GetPositionTarget(K);Source.Add(T.SourceIndex);Target.Add(T.TargetIndex);
        SW.Add(T.SourceWeights);TW.Add(T.TargetWeights);Stiffness.Add(T.Stiffness);Aniso.Add(T.bIsAnisotropic);Zero.Add(T.bIsZeroRestLength);
    }
    auto Weak=MakeShared<FGaussSeidelSpringConstraints>(Source,SW,Stiffness,Target,TW,Aniso,Zero,FDeformableXPBDWeightedSpringConstraintParams());
    Weak->ComputeInitialWCData(RestParticles);
    const TArray<TArray<int32>> *WI=nullptr,*WL=nullptr;
    const auto& WC=Weak->GetStaticConstraintArrays(WI,WL);Main->AddStaticConstraints(WC,*WI,*WL);
    Index=Main->AddStaticConstraintResidualAndHessianRange(1);
    Main->StaticConstraintResidualAndHessian()[Index]=[Weak](const FSolverParticlesRange& P,int T,int L,FSolverReal Dt,Vec& R,Mat& H){Weak->AddWCResidual(P,T,L,Dt,R,H);};
    Index=Main->AddPerNodeHessianRange(1);
    Main->PerNodeHessian()[Index]=[Weak](int P,FSolverReal Dt,Mat& H){Weak->AddWCHessian(P,Dt,H);};
    Main->InitStaticColor(E.Particles(),&E.ParticlesActiveView());
    Index=E.AddConstraintInitRange(1,true);
    E.ConstraintInits()[Index]=[Main,Weak](FSolverParticles& P,FSolverReal Dt){Main->Init(Dt,P);Weak->Init(P,Dt);};
    E.ConstraintRules()[0]=[Main,&E](FSolverParticles& P,FSolverReal Dt){Main->Apply(P,Dt,10,false,&E.ParticlesActiveView());};
    return true;
}

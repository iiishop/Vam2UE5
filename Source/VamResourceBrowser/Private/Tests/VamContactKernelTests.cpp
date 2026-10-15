#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "VamRuntimeConfiguration.h"
#include "VamBreastContactProfile.h"
#include "Chaos/Deformable/GaussSeidelCorotatedConstraints.h"
#include "Chaos/Deformable/GaussSeidelMainConstraint.h"

namespace VamContactKernel
{
using namespace Chaos::Softs;
using Mat = Chaos::PMatrix<FSolverReal,3,3>;
using Vec = Chaos::TVec3<FSolverReal>;
// Diagnostic-only subclass: invokes the installed engine's original protected
// callbacks. Ablations NEVER update a simulated character or production solver.
class FKernelMaterial : public FGaussSeidelCorotatedTetrahedralConstraints
{
public:
    using FGaussSeidelCorotatedTetrahedralConstraints::FGaussSeidelCorotatedTetrahedralConstraints;
    decltype(ComputeStress) SavedStress;
    decltype(ComputeHessianHelper) SavedHessian;
    void Save() { SavedStress=ComputeStress; SavedHessian=ComputeHessianHelper; }
    void Mode(int32 Mode)
    {
        ComputeStress=SavedStress; ComputeHessianHelper=SavedHessian;
        if(Mode&1) ComputeStress=[](const Mat&,FSolverReal,FSolverReal,Mat& P){P=Mat(FSolverReal(0));};
        if(Mode&2) ComputeHessianHelper=[](const Mat&,const Mat&,FSolverReal,FSolverReal,int32,FSolverReal,Mat&){};
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVamContactKernelTest,"Vam.Breast.ContactKernel",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FVamContactKernelTest::RunTest(const FString&)
{
    using namespace VamContactKernel;
    FString Path;
    if(!FParse::Value(FCommandLine::Get(),TEXT("VamBreastTestConfig="),Path)) { AddError(TEXT("Explicit profile configuration required"));return false; }
    auto* Config=LoadObject<UVamRuntimeConfiguration>(nullptr,*Path);
    auto* Profile=Config?Config->BreastContact.LoadSynchronous():nullptr;
    if(!TestNotNull(TEXT("Profile"),Profile))return false;
    FSolverParticles X; X.AddParticles(Profile->Particles.Num());
    for(int I=0;I<Profile->Particles.Num();++I)
    {
        X.X(I)=Vec(Profile->Particles[I].Rest);X.P(I)=X.X(I);X.V(I)=Vec(0);
        X.M(I)=1;X.InvM(I)=Profile->Particles[I].bKinematic?0:1;
    }
    TArray<Chaos::TVector<int32,4>> Mesh;
    for(const auto& T:Profile->Tetrahedra)Mesh.Add(Chaos::TVector<int32,4>(T[0],T[1],T[2],T[3]));
    TArray<FSolverReal> Young,Nu,Alpha;
    Young.Init(Profile->YoungModulusPa,Mesh.Num());Nu.Init(Profile->PoissonRatio,Mesh.Num());Alpha.Init(1,Mesh.Num());
    TArray<TArray<int32>> Incident,Local;
    Incident.SetNum(X.Size());Local.SetNum(X.Size());
    for(int E=0;E<Mesh.Num();++E)for(int J=0;J<4;++J){Incident[Mesh[E][J]].Add(E);Local[Mesh[E][J]].Add(J);}
    FKernelMaterial Material(X,Mesh,Young,Nu,MoveTemp(Alpha),MoveTemp(Incident),MoveTemp(Local),0,X.Size(),true,false);
    Material.Save();
    const FSolverReal Dt=1.f/120;
    FString CSV=TEXT("state,repeat,mode,calls,elapsed_ms,checksum\n");
    // Frozen diagnostic fields on real topology, NOT physical pressure solutions.
    // Vary deformation to avoid benchmarking only the identity polar fast path.
    for(int State=0;State<3;++State)
    {
        for(int I=0;I<int32(X.Size());++I)
        {
            const Vec R=X.X(I); X.P(I)=R;
            if(State)X.P(I)=Vec(R.X*(State==1?.85f:.65f)+.08f*FMath::Sin(R.Z*.2f),R.Y*1.08f,R.Z*1.07f+.07f*R.Y);
        }
        FSolverParticlesRange Range(X);
        for(int Repeat=-1;Repeat<7;++Repeat)for(int Order=0;Order<4;++Order)
        {
            const int Mode=Repeat%2==0?Order:3-Order;Material.Mode(Mode);
            double Check=0;int64 Calls=0;const double Start=FPlatformTime::Seconds();
            for(int Pass=0;Pass<24;++Pass)for(int P=0;P<int32(X.Size());++P)if(X.InvM(P)>0)
            {
                Vec Residual(0);Mat Hessian(FSolverReal(0));
                const auto& Inc=Material.GetIncidentElements()[P];const auto& Loc=Material.GetIncidentElementsLocal()[P];
                for(int K=0;K<Inc.Num();++K){Material.AddHyperelasticResidualAndHessian(Range,Inc[K],Loc[K],Dt,Residual,Hessian);++Calls;}
                Check+=Residual.X+Hessian.GetAt(0,0);
            }
            const double Ms=(FPlatformTime::Seconds()-Start)*1000;
            TestTrue(TEXT("Finite diagnostic result"),FMath::IsFinite(Check));
            if(Repeat>=0)CSV+=FString::Printf(TEXT("%d,%d,%d,%lld,%.9f,%.9g\n"),State,Repeat,Mode,Calls,Ms,Check);
        }
    }
    FString Out=FPaths::ProjectSavedDir()/TEXT("ContactKernel.csv");
    FParse::Value(FCommandLine::Get(),TEXT("VamContactKernelOutput="),Out);
    TestTrue(TEXT("Save kernel timings"),FFileHelper::SaveStringToFile(CSV,*Out));
    // The actual public GS class accepts batching parameters. Its owning Flesh
    // solver currently does not forward the corresponding global CVar.
    // Exercise this supported API on a material-only problem, not a character.
    Material.Mode(0);
    FString BatchCSV=TEXT("batch,repeat,elapsed_ms,max_position_error_cm\n");
    TArray<Vec> Reference;
    for(int Batch:{5,1,16,32,64,512,1,5})
    {
        FDeformableXPBDCorotatedParams Params;Params.XPBDCorotatedBatchSize=Batch;
        FGaussSeidelMainConstraints Main(X,true,false,1.6f,1000,1,Params);
        Main.AddStaticConstraints(Material.GetMeshArray(),Material.GetIncidentElements(),Material.GetIncidentElementsLocal());
        const int Rule=Main.AddStaticConstraintResidualAndHessianRange(1);
        Main.StaticConstraintResidualAndHessian()[Rule]=[&Material](const FSolverParticlesRange& P,int E,int L,FSolverReal D,Vec& R,Mat& H)
        {Material.AddHyperelasticResidualAndHessian(P,E,L,D,R,H);};
        Main.InitStaticColor(X);
        for(int Repeat=-1;Repeat<7;++Repeat)
        {
            for(int I=0;I<int32(X.Size());++I){const Vec R=X.X(I);X.P(I)=Vec(R.X*.8f,R.Y*1.08f,R.Z*1.07f+.07f*R.Y);}
            Main.Init(Dt,X);
            const double Start=FPlatformTime::Seconds();
            for(int It=0;It<12;++It)Main.Apply(X,Dt);
            const double Ms=(FPlatformTime::Seconds()-Start)*1000;
            if(Reference.IsEmpty())for(int I=0;I<int32(X.Size());++I)Reference.Add(X.P(I));
            double Error=0;
            for(int I=0;I<int32(X.Size());++I)Error=FMath::Max(Error,double((X.P(I)-Reference[I]).Size()));
            TestTrue(TEXT("Batching preserves native GS material trajectory endpoint"),FMath::IsFinite(Error)&&Error<1.e-5);
            if(Repeat>=0)BatchCSV+=FString::Printf(TEXT("%d,%d,%.9f,%.9g\n"),Batch,Repeat,Ms,Error);
        }
    }
    TestTrue(TEXT("Save GS batching timings"),FFileHelper::SaveStringToFile(BatchCSV,*(Out+TEXT(".batch.csv"))));
    AddInfo(TEXT("Native kernel ablation complete; serial frozen fields, not full solver wall time. 0=full, 1=no stress, 2=no Hessian, 3=neither."));
    return true;
}
#endif

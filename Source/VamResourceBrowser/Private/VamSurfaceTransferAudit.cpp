#include "VamGluteStructureBuilder.h"
#include "VamBreastJiggleBuilder.h"
#include "VamNativeBuilder.h"
#include "VamCharacterDefinition.h"
#include "VamGluteStructureProfile.h"
#include "VamBreastJiggleProfile.h"
#include "VamGluteCorrectiveAudit.h"

FString UVamGluteStructureBuilder::SurfaceTransferAudit(UVamCharacterDefinition* D,UVamGluteStructureProfile* G,UVamBreastJiggleProfile* B)
{
    FVamNativeMeshInput I;FString Error;if(!D || !G || !B || !UVamBreastJiggleBuilder::ExtractNative(D->Body.LoadSynchronous(),I,Error)) return TEXT("{}");
    TArray<double> Areas;Areas.Init(0,I.Vertices.Num());
    for(int32 T=0;T<I.Triangles.Num();T+=3)
    {
        const int32 A=I.Triangles[T],C=I.Triangles[T+1],E=I.Triangles[T+2];
        const double Share=FVector::CrossProduct(I.Vertices[C]-I.Vertices[A],I.Vertices[E]-I.Vertices[A]).Size()/6;
        for(int32 V:{A,C,E}) Areas[V]+=Share;
    }
    auto Audit=[&](const TArray<float>& Mask,const TSet<int32>& Helpers)
    {
        auto O=MakeShared<FJsonObject>();TArray<double> Transfer;Transfer.Init(0,I.Vertices.Num());
        for(const auto& W:I.Influences) if(Helpers.Contains(W.Bone)) Transfer[W.Vertex]+=W.Weight;
        double MaxMask=0,Sum=0,Mean=0,CoreArea=0,CoreMean=0,MaxTransfer=0;TArray<TPair<double,double>> Distribution;
        for(int32 V=0;V<Mask.Num();++V) if(Areas[V]>0) MaxMask=FMath::Max(MaxMask,double(Mask[V]));
        for(int32 V=0;V<Mask.Num();++V) if(Areas[V]>0 && Mask[V]>.001)
        {
            const double W=Areas[V]*Mask[V];Sum+=W;Mean+=W*Transfer[V];MaxTransfer=FMath::Max(MaxTransfer,Transfer[V]);Distribution.Emplace(Transfer[V],W);
            if(Mask[V]>=MaxMask*.5) { CoreArea+=Areas[V];CoreMean+=Areas[V]*Transfer[V]; }
        }
        Distribution.Sort([](const auto& A,const auto& C){return A.Key<C.Key;});
        O->SetNumberField(TEXT("max_region"),MaxMask);O->SetNumberField(TEXT("region_area_cm2"),Sum);
        O->SetNumberField(TEXT("mean_transfer"),Mean/FMath::Max(1.e-12,Sum));O->SetNumberField(TEXT("core_transfer"),CoreMean/FMath::Max(1.e-12,CoreArea));O->SetNumberField(TEXT("max_transfer"),MaxTransfer);
        for(int32 Percent:{10,50,90}) { double A=0,Q=0;for(const auto& X:Distribution) { A+=X.Value;Q=X.Key;if(A>=Sum*Percent/100.) break; }O->SetNumberField(FString::Printf(TEXT("p%d_transfer"),Percent),Q); }
        return O;
    };
    auto Result=MakeShared<FJsonObject>();Result->SetStringField(TEXT("meaning"),TEXT("Exact native skin response to coherent 1 cm helper translation; area x region weighted. No solver, rotation or visual scoring."));
    for(const auto& S:G->Sides) {TSet<int32> Bones;for(const auto& R:S.Regions) Bones.Add(R.BoneIndex);Result->SetObjectField(TEXT("Glute_")+S.Side.ToString(),Audit(S.RegionWeights,Bones));}
    for(const auto& S:B->Sides) {TSet<int32> Bones;for(const auto& R:S.Nodes) Bones.Add(R.BoneIndex);Result->SetObjectField(TEXT("Breast_")+S.Side.ToString(),Audit(S.RegionWeights,Bones));}
    return VamGluteAudit::Json(Result);
}

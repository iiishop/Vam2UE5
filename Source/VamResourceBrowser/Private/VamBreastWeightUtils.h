#pragma once
#include "CoreMinimal.h"

namespace VamBreastWeights
{
/** Returns compressed donor count, or -1 if no legal donor/slot exists. Non-donors are untouched. */
inline int32 Redistribute(TMap<int32,double>& Weights,const TSet<int32>& Donors,TArray<TPair<int32,double>> Helpers,double Transfer)
{
    if(Transfer<=1.e-8 || Helpers.IsEmpty()) return -1;
    TArray<TPair<int32,double>> Eligible;
    double DonorSum=0;
    for(const auto& W:Weights) if(Donors.Contains(W.Key)) { Eligible.Add(W);DonorSum+=W.Value; }
    if(Eligible.IsEmpty() || DonorSum<=1.e-10) return -1;
    Eligible.Sort([](const auto& A,const auto& B){return A.Value==B.Value?A.Key<B.Key:A.Value<B.Value;});
    // Consolidate small donors into the strongest approved donor, freeing at least one slot at eight influences.
    int32 Compressed=0;
    const int32 Keep=Eligible.Last().Key;
    while(Weights.Num()>=8 && Compressed<Eligible.Num()-1)
    {
        const auto& Small=Eligible[Compressed++];Weights.FindChecked(Keep)+=Small.Value;Weights.Remove(Small.Key);
    }
    if(Weights.Num()>=8) return -1; // Seven non-donors + one donor: preserve all non-donors and root support.
    Helpers.Sort([](const auto& A,const auto& B){return A.Value==B.Value?A.Key<B.Key:A.Value>B.Value;});
    Helpers.SetNum(FMath::Min(8-Weights.Num(),Helpers.Num()));
    double Sum=0;for(const auto& H:Helpers) Sum+=H.Value;
    if(Sum<=1.e-30) return -1;
    double Moved=0;
    for(auto& W:Weights) if(Donors.Contains(W.Key)) { const double Amount=W.Value*FMath::Clamp(Transfer,0.,.95);W.Value-=Amount;Moved+=Amount; }
    for(const auto& H:Helpers) if(H.Value>0) Weights.Add(H.Key,Moved*H.Value/Sum);
    return Compressed;
}
/** Apply the native eight-influence budget only after all continuous weight fitting. */
inline double FinalizeBudget(TMap<int32,double>& Weights)
{
    TArray<TPair<int32,double>> Ranked;for(const auto& W:Weights)if(W.Value>0)Ranked.Add(W);
    Ranked.Sort([](const auto& A,const auto& B){return A.Value==B.Value?A.Key<B.Key:A.Value>B.Value;});
    double Kept=0,Discarded=0;for(int32 N=0;N<Ranked.Num();++N)(N<8?Kept:Discarded)+=Ranked[N].Value;
    if(Kept<=1.e-12)return 0;
    Weights.Reset();for(int32 N=0;N<FMath::Min(8,Ranked.Num());++N)Weights.Add(Ranked[N].Key,Ranked[N].Value/Kept);
    return Discarded;
}
/** Glute scaffolds have different static transforms. Allocate the finite influence
 * budget from the COMPLETE final candidate, rather than reserving a slot for an
 * arbitrarily tiny old influence and deleting a substantial helper instead.
 * Existing breast transfer keeps its explicit non-donor preservation contract.
 * Returns discarded candidate mass (before normalization).
 */
inline double RedistributeFinalBudget(TMap<int32,double>& Weights,const TSet<int32>& Donors,const TArray<TPair<int32,double>>& Helpers,double Transfer)
{
    if(Transfer<=1.e-8 || Helpers.IsEmpty()) return 0;
    double KernelSum=0,DonorSum=0;for(const auto& H:Helpers)KernelSum+=FMath::Max(0.,H.Value);
    for(const auto& W:Weights)if(Donors.Contains(W.Key))DonorSum+=W.Value;
    if(KernelSum<=1.e-30 || DonorSum<=1.e-10)return 0;
    const double Fraction=FMath::Clamp(Transfer,0.,.95),Moved=DonorSum*Fraction;
    auto Candidate=Weights;for(auto& W:Candidate)if(Donors.Contains(W.Key))W.Value*=1-Fraction;
    for(const auto& H:Helpers)if(H.Value>0)Candidate.FindOrAdd(H.Key)+=Moved*H.Value/KernelSum;
    Weights=MoveTemp(Candidate);return FinalizeBudget(Weights);
}

}

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
}

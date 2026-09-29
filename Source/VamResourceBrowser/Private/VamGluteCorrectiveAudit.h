#pragma once
#include "VamGluteStructureProfile.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"

// Editor-only surface statistics. Source IDs deduplicate UV aliases; zero
// displacements remain in percentiles so sparse fields cannot hide attenuation.
namespace VamGluteAudit
{
inline TSharedPtr<FJsonObject> Distribution(const TArray<FVector>& Field,const TArray<double>& Mask,const TArray<int32>& Ids)
{
    TSet<int32> Seen;TArray<TPair<double,double>> Values;double Total=0,Energy=0;FVector Mean=FVector::ZeroVector,AxisEnergy=FVector::ZeroVector;int32 Affected=0;
    for(int32 V=0;V<Field.Num();++V) if(Mask[V]>1.e-6 && !Seen.Contains(Ids[V]))
    {
        Seen.Add(Ids[V]);const double L=Field[V].Size(),W=Mask[V];Values.Emplace(L,W);Total+=W;Energy+=W*L*L;Mean+=Field[V]*W;AxisEnergy+=Field[V]*Field[V]*W;if(L>1.e-6) ++Affected;
    }
    Values.Sort([](const auto& A,const auto& B){return A.Key<B.Key;});auto O=MakeShared<FJsonObject>();
    O->SetNumberField(TEXT("domain_vertices"),Values.Num());O->SetNumberField(TEXT("affected_vertices"),Affected);
    for(int32 P:{50,75,90,95,99}) { double Sum=0,Q=0;for(const auto& X:Values) { Sum+=X.Value;Q=X.Key;if(Sum>=Total*P/100.) break; }O->SetNumberField(FString::Printf(TEXT("p%d"),P),Q); }
    O->SetNumberField(TEXT("max"),Values.IsEmpty()?0:Values.Last().Key);O->SetNumberField(TEXT("rms"),FMath::Sqrt(Energy/FMath::Max(1.e-12,Total)));
    TArray<TSharedPtr<FJsonValue>> M,E;for(int32 A=0;A<3;++A) { M.Add(MakeShared<FJsonValueNumber>(Mean[A]/FMath::Max(1.e-12,Total)));E.Add(MakeShared<FJsonValueNumber>(FMath::Sqrt(AxisEnergy[A]/FMath::Max(1.e-12,Total)))); }O->SetArrayField(TEXT("mean"),M);O->SetArrayField(TEXT("axis_rms"),E);return O;
}
inline TArray<double> Mask(const FVamGluteSide& S,int32 Region)
{
    TArray<double> W;W.SetNum(S.RegionPoints.Num());for(int32 V=0;V<W.Num();++V)
    {
        const FVector Q=(S.RegionPoints[V]-S.Regions[2].Rest)/S.Dimensions;double K=1;
        if(Region>=0 && Region<5) K=FMath::Exp(-((S.RegionPoints[V]-S.Regions[Region].Rest)/S.Dimensions).SizeSquared()*12);
        if(Region==5) K=FMath::Exp(-2*FMath::Square(Q.Z/.19)-2*FMath::Square(Q.Y/.48));
        // Camera-independent posterior/lateral contour proxy, not a rendered silhouette measurement.
        if(Region==6) K=FMath::SmoothStep(.15,.45,FMath::Abs((S.RegionPoints[V].Y-S.Regions[0].Rest.Y)/S.Dimensions.Y))+FMath::SmoothStep(.1,.4,(S.RegionPoints[V].X-S.Regions[0].Rest.X)/S.Dimensions.X);
        W[V]=S.RegionWeights[V]*FMath::Min(1.,K);
    }return W;
}
inline TSharedPtr<FJsonObject> Regions(const TArray<FVector>& Field,const FVamGluteSide& S,const TArray<int32>& Ids)
{
    auto O=MakeShared<FJsonObject>();const TCHAR* Names[]={TEXT("whole"),TEXT("Core"),TEXT("Upper"),TEXT("Lower"),TEXT("Medial"),TEXT("Lateral"),TEXT("under_curve"),TEXT("silhouette_proxy")};
    for(int32 N=-1;N<7;++N) O->SetObjectField(Names[N+1],Distribution(Field,Mask(S,N),Ids));return O;
}
inline FString Json(const TSharedPtr<FJsonObject>& O) { FString S;FJsonSerializer::Serialize(O.ToSharedRef(),TJsonWriterFactory<>::Create(&S));return S; }
}

#pragma once
#include "VamGluteCorrectiveAudit.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
// Opt-in, external diagnostic data only. Never stored in or evaluated by a live solver.
struct FVamGluteCorrectiveDetail
{
    FString Directory;
    TSharedPtr<FJsonObject> Meta=MakeShared<FJsonObject>(),Fields=MakeShared<FJsonObject>();
    TArray<TArray<TArray<double>>> SourceSafety,ResidualSafety;
    bool bSaved=true;
    explicit FVamGluteCorrectiveDetail(const FString& D):Directory(D){IFileManager::Get().MakeDirectory(*D,true);SourceSafety.SetNum(2);ResidualSafety.SetNum(2);}
    FString Key(int S,int T,const FString& Name)const{return FString::Printf(TEXT("s%d_t%d_"),S,T)+Name;}
    void Numbers(const FString& Name,const TArray<float>& Values,int Components,const FString& Space)
    {
        TArray<uint8> Bytes;Bytes.SetNumUninitialized(Values.Num()*sizeof(float));FMemory::Memcpy(Bytes.GetData(),Values.GetData(),Bytes.Num());
        bSaved=FFileHelper::SaveArrayToFile(Bytes,*(Directory/(Name+TEXT(".f32"))))&&bSaved;
        auto F=MakeShared<FJsonObject>();F->SetStringField(TEXT("file"),Name+TEXT(".f32"));F->SetNumberField(TEXT("components"),Components);F->SetNumberField(TEXT("count"),Values.Num()/Components);F->SetStringField(TEXT("space"),Space);Fields->SetObjectField(Name,F);
    }
    void Field(const FString& Name,const TArray<FVector>& V,const FString& Space=TEXT("input_preskin_delta_cm")){TArray<float> A;A.Reserve(V.Num()*3);for(const auto& P:V){A.Add(P.X);A.Add(P.Y);A.Add(P.Z);}Numbers(Name,A,3,Space);}
    void Scalar(const FString& Name,const TArray<double>& V){TArray<float>A;for(double X:V)A.Add(X);Numbers(Name,A,1,TEXT("dimensionless"));}
    void Begin(int S,int T,int Count){SourceSafety[S].SetNum(Count);ResidualSafety[S].SetNum(Count);}
    void Project(int S,int T,const FString& Name,const TArray<FVector>& Native,const TArray<FMatrix>& Skin,const FTransform& Anchor)
    {
        for(int A=0;A<3;++A){TArray<FVector> D;D.SetNumZeroed(Native.Num());for(int V=0;V<D.Num();++V){if(Native[V].IsNearlyZero(1.e-12)||FMath::Abs(Skin[V].Determinant())<.01)continue;const FVector P=Anchor.InverseTransformVectorNoScale(FVector(Skin[V].TransformVector(Native[V])));FVector X=FVector::ZeroVector;X[A]=P[A];D[V]=FVector(Skin[V].Inverse().TransformVector(Anchor.TransformVectorNoScale(X)));}Field(Key(S,T,Name+FString::Printf(TEXT("_axis%d"),A)),D);}
    }
    bool Save(){Meta->SetStringField(TEXT("schema"),TEXT("vam-corrective-detail/1"));Meta->SetStringField(TEXT("binary_format"),TEXT("little-endian float32; full input vertex order; dimensions in field metadata"));Meta->SetObjectField(TEXT("fields"),Fields);FString J;FJsonSerializer::Serialize(Meta.ToSharedRef(),TJsonWriterFactory<TCHAR,TCondensedJsonPrintPolicy<TCHAR>>::Create(&J));return FFileHelper::SaveStringToFile(J,*(Directory/TEXT("detail.json")))&&bSaved;}
};

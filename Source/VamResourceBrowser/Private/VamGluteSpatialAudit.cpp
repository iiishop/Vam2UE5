#include "VamGluteStructureBuilder.h"
#include "VamBreastJiggleBuilder.h"
#include "VamNativeBuilder.h"
#include "VamCharacterDefinition.h"
#include "VamGluteStructureProfile.h"
#include "VamGluteCorrectiveAudit.h"

FString UVamGluteStructureBuilder::SpatialAudit(UVamCharacterDefinition* D,UVamGluteStructureProfile* P)
{
    FVamNativeMeshInput I;FString Error;if(!D || !P || !UVamBreastJiggleBuilder::ExtractNative(D->Body.LoadSynchronous(),I,Error)) return TEXT("{}");
    TArray<double> Areas;Areas.Init(0,I.Vertices.Num());TSet<int32> Used;
    for(int32 T=0;T<I.Triangles.Num();T+=3)
    {
        const int32 A=I.Triangles[T],B=I.Triangles[T+1],C=I.Triangles[T+2];const double Area=FVector::CrossProduct(I.Vertices[B]-I.Vertices[A],I.Vertices[C]-I.Vertices[A]).Size()/6;
        for(int32 V:{A,B,C}) {Areas[V]+=Area;Used.Add(V);}
    }
    auto Result=MakeShared<FJsonObject>();Result->SetNumberField(TEXT("unused_vertices"),I.Vertices.Num()-Used.Num());bool Valid=true;
    auto Vector=[](const FVector& V){TArray<TSharedPtr<FJsonValue>> A;for(int32 J=0;J<3;++J) A.Add(MakeShared<FJsonValueNumber>(V[J]));return A;};
    for(const auto& S:P->Sides)
    {
        if(S.RegionWeights.Num()!=I.Vertices.Num() || S.RegionPoints.Num()!=I.Vertices.Num()) return TEXT("{\"valid\":false}");
        auto Side=MakeShared<FJsonObject>();TArray<TSharedPtr<FJsonValue>> Nodes;double Sum=0,UsedSum=0,AreaSum=0;FVector Mean=FVector::ZeroVector,UsedMean=Mean,AreaMean=Mean;
        for(int32 V=0;V<S.RegionWeights.Num();++V)
        {
            const double W=S.RegionWeights[V];const FVector X=S.RegionPoints[V];Sum+=W;Mean+=X*W;
            if(Used.Contains(V)) {UsedSum+=W;UsedMean+=X*W;}AreaSum+=W*Areas[V];AreaMean+=X*W*Areas[V];
        }
        Side->SetNumberField(TEXT("total_region_weight"),Sum);Side->SetNumberField(TEXT("used_region_weight"),UsedSum);
        Side->SetNumberField(TEXT("unused_region_weight"),Sum-UsedSum);Valid&=FMath::Abs(Sum-UsedSum)<1.e-8 && AreaSum>1.e-8;
        Side->SetArrayField(TEXT("vertex_mean"),Vector(Mean/FMath::Max(1.e-12,Sum)));Side->SetArrayField(TEXT("used_vertex_mean"),Vector(UsedMean/FMath::Max(1.e-12,UsedSum)));Side->SetArrayField(TEXT("surface_area_mean"),Vector(AreaMean/FMath::Max(1.e-12,AreaSum)));
        for(const auto& R:S.Regions)
        {
            auto Node=MakeShared<FJsonObject>();Node->SetStringField(TEXT("semantic"),R.Semantic.ToString());Node->SetArrayField(TEXT("rest"),Vector(R.Rest));
            double W=0;FVector Center=FVector::ZeroVector;
            for(const auto& F:I.Influences) if(F.Bone==R.BoneIndex && Used.Contains(F.Vertex)) {const double K=F.Weight*Areas[F.Vertex];W+=K;Center+=S.RegionPoints[F.Vertex]*K;}
            Center/=FMath::Max(1.e-12,W);const double SpatialError=((R.Rest-Center)/S.Dimensions).Size();
            Node->SetNumberField(TEXT("skin_support_area"),W);Node->SetArrayField(TEXT("skin_support_center"),Vector(Center));
            Node->SetNumberField(TEXT("support_error_cm"),(R.Rest-Center).Size());Node->SetNumberField(TEXT("support_error_normalized"),SpatialError);
            // Regional kernels and donor compression differ, but the helper must
            // stay within one quarter of the anatomical dimensions of its support.
            Valid&=W>1.e-8 && FMath::IsFinite(SpatialError) && SpatialError<.25;Nodes.Add(MakeShared<FJsonValueObject>(Node));
        }
        Side->SetArrayField(TEXT("nodes"),Nodes);Result->SetObjectField(S.Side.ToString(),Side);
    }
    Result->SetBoolField(TEXT("valid"),Valid);return VamGluteAudit::Json(Result);
}

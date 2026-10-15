#include "VamKneeCorrectiveBuilder.h"
#include "VamGluteCorrectiveGeometry.h"
#include "VamMorphNormalUtils.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

bool VamKneeBuilder::Build(FVamNativeMeshInput& I,const TArray<int32>& Ids,UVamGluteCorrectiveProfile& P,const FString& Family,FString& Error)
{
    using namespace VamGluteGeometry;
    TSharedPtr<FJsonObject> F;const TSharedPtr<FJsonObject>* Source=nullptr;
    if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Family),F)||!F->TryGetObjectField(TEXT("knee_corrective_source"),Source))return true;
    P.Knees.Reset();P.KneeSourceDigest.Reset();
    if((*Source)->GetStringField(TEXT("mode"))!=TEXT("source_triax_knee_x"))return true;
    P.KneeSourceDigest=(*Source)->GetStringField(TEXT("digest"));
    const double Bulge=(*Source)->GetNumberField(TEXT("bulge_scale"));
    TArray<FTransform> Bind;for(const auto& B:I.Bones)Bind.Add(B.Parent<0?B.LocalBind:B.LocalBind*Bind[B.Parent]);
    TMap<int32,TArray<int32>> Aliases;for(int V=0;V<Ids.Num();++V)if(Ids[V]>=0)Aliases.FindOrAdd(Ids[V]).Add(V);
    for(const auto& NameValue:(*Source)->GetArrayField(TEXT("names")))
    {
        const FString Name=NameValue->AsString();const int Bone=I.Bones.IndexOfByPredicate([&](const auto& B){return B.Name.ToString()==Name;});
        if(Bone<0||I.Bones[Bone].Parent<0){Error=TEXT("Knee source bone missing");return false;}
        FVamKneeCorrective K;K.Bone=Bone;K.Parent=I.Bones[Bone].Parent;K.ReferenceRotation=I.Bones[Bone].LocalBind.GetRotation();
        TArray<double> Weight,Left,Right;Weight.Init(0,I.Vertices.Num());Left=Right=Weight;TArray<uint8> Active;Active.Init(0,I.Vertices.Num());
        double PositiveLeft=0,PositiveRight=0,NegativeLeft=0,NegativeRight=0;
        for(const auto& NodeValue:(*Source)->GetArrayField(TEXT("nodes")))
        {
            const auto Node=NodeValue->AsObject();if(Node->GetStringField(TEXT("name"))!=Name)continue;
            const auto Factors=Node->GetObjectField(TEXT("bulgeFactors"));
            PositiveLeft=Factors->GetNumberField(TEXT("xposleft"));PositiveRight=Factors->GetNumberField(TEXT("xposright"));NegativeLeft=Factors->GetNumberField(TEXT("xnegleft"));NegativeRight=Factors->GetNumberField(TEXT("xnegright"));
            auto Set=[&](int Id,double W,double L,double R){if(const auto* Vs=Aliases.Find(Id))for(int V:*Vs){Active[V]=1;Weight[V]=W;Left[V]=L;Right[V]=R;}};
            for(const auto& Value:Node->GetArrayField(TEXT("weights"))){const auto W=Value->AsObject();Set(W->GetIntegerField(TEXT("vertex")),W->GetNumberField(TEXT("xweight")),W->GetNumberField(TEXT("xleftbulge")),W->GetNumberField(TEXT("xrightbulge")));}
            for(const auto& Value:Node->GetArrayField(TEXT("fullyWeightedVertices")))Set(Value->AsNumber(),1,0,0);
        }
        for(int Degrees=-15;Degrees<=150;Degrees+=15)
        {
            // UE drops an all-zero morph. Neutral is an explicit zero basis,
            // not a fake vertex displacement to keep a target alive.
            if(Degrees==0){K.Angles.Add(0);K.Morphs.Add(NAME_None);continue;}
            const double Angle=Degrees*PI/180.;auto PoseCS=Bind;
            for(int B=0;B<I.Bones.Num();++B)
            {
                auto Local=I.Bones[B].LocalBind;if(B==Bone)Local.SetRotation(Local.GetRotation()*FQuat(FVector::YAxisVector,Angle));
                PoseCS[B]=I.Bones[B].Parent<0?Local:Local*PoseCS[I.Bones[B].Parent];
            }
            const auto M=Skin(I,I.Influences,Bind,PoseCS);const auto Base=Apply(M,I.Vertices,true);
            FVamBuildMorph Morph;Morph.Name=FName(*FString::Printf(TEXT("Knee_%s_%d"),*Name,Degrees));Morph.Deltas.Init(FVector::ZeroVector,I.Vertices.Num());
            double Maximum=0;
            for(int V=0;V<I.Vertices.Num();++V)if(Active[V] && Degrees!=0)
            {
                FVector Q=Bind[Bone].InverseTransformPosition(I.Vertices[V]);
                Q=FQuat(FVector::YAxisVector,Angle*Weight[V]).RotateVector(Q);
                if(FMath::Abs(Angle)>.01){const double S=(1+(Angle>0?PositiveLeft:NegativeLeft)*Angle*Bulge*Left[V])*(1+(Angle>0?PositiveRight:NegativeRight)*Angle*Bulge*Right[V]);Q.X*=S;Q.Z*=S;}
                const FVector Target=Bind[Bone].TransformPosition(Q);const FVector Delta=Target-Base[V];
                if(FMath::Abs(M[V].Determinant())<.01){Error=TEXT("Knee target has singular final skin operator");return false;}
                Morph.Deltas[V]=FVector(M[V].Inverse().TransformVector(Delta));Maximum=FMath::Max(Maximum,Delta.Size());
            }
            Weld(Morph.Deltas,Aliases);
            // Keep the established local triangle safety checks. Do not silently
            // publish a folded target or disable the check to pass a screenshot.
            TArray<double> Safety;if(!Condition(I,M,Morph.Deltas,Aliases,Safety)){Error=TEXT("Knee source residual safety failed");return false;}
            double MinimumSafety=1;for(int V=0;V<Morph.Deltas.Num();++V){Morph.Deltas[V]*=Safety[V];if(!Morph.Deltas[V].IsNearlyZero())MinimumSafety=FMath::Min(MinimumSafety,Safety[V]);}
            Morph.NormalDeltas=VamMorphNormals::Deltas(I.Vertices,I.Normals,I.Triangles,Morph.Deltas);
            K.Angles.Add(Degrees);K.Morphs.Add(Morph.Name);I.Morphs.Add(MoveTemp(Morph));
            UE_LOG(LogTemp,Display,TEXT("KNEE_OPERATOR %s angle=%d posed_max=%.6f minimum_safety=%.6f"),*Name,Degrees,Maximum,MinimumSafety);
        }
        P.Knees.Add(MoveTemp(K));
    }
    return true;
}

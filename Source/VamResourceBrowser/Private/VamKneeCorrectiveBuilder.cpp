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
        FDeltaField PreviousTarget=I.Vertices;
        for(int Degrees=-15;Degrees<=165;Degrees+=5)
        {
            // UE drops an all-zero morph. Neutral is an explicit zero basis,
            // not a fake vertex displacement to keep a target alive.
            if(Degrees==0){K.Angles.Add(0);K.Morphs.Add(NAME_None);PreviousTarget=I.Vertices;continue;}
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
            // Validate the source operator along its angular continuation. Collapsed
            // LBS is not an orientation reference for a volume-restoring target;
            // pre-skin inverse deltas also grow as LBS approaches singularity.
            // Reject unsafe source steps/reconstruction rather than spatially
            // attenuating a valid target back toward that collapsed surface.
            const auto Reconstructed=Apply(M,Morph.Deltas);
            FDeltaField Target;Target.SetNum(I.Vertices.Num());
            for(int V=0;V<Target.Num();++V)
            {
                Target[V]=Base[V]+Reconstructed[V];
                if(Target[V].ContainsNaN()){Error=TEXT("Nonfinite knee source target");return false;}
            }
            const auto SourceTarget=Target;
            // Apply the existing triangle/edge guard in posed incremental space.
            // Its feasible fallback is the previous valid source pose, not LBS.
            FVamNativeMeshInput StepInput=I;StepInput.Vertices=PreviousTarget;
            TArray<FMatrix> Identity;Identity.Init(FMatrix::Identity,Target.Num());
            FDeltaField Increment;Increment.SetNum(Target.Num());
            for(int V=0;V<Target.Num();++V)Increment[V]=Target[V]-PreviousTarget[V];
            TArray<double> StepSafety;
            if(!Condition(StepInput,Identity,Increment,Aliases,StepSafety))
            {Error=FString::Printf(TEXT("Knee source continuation unsafe: %s angle=%d"),*Name,Degrees);return false;}
            for(int V=0;V<Target.Num();++V)Target[V]=PreviousTarget[V]+Increment[V]*StepSafety[V];
            double RepairMaximum=0,ReconstructionMaximum=0;
            for(int V=0;V<Target.Num();++V)
            {
                RepairMaximum=FMath::Max(RepairMaximum,(Target[V]-SourceTarget[V]).Size());
                const FVector Delta=Target[V]-Base[V];
                if(!Delta.IsNearlyZero(1.e-12) && FMath::Abs(M[V].Determinant())<.01){Error=TEXT("Knee repaired target singular skin operator");return false;}
                Morph.Deltas[V]=Delta.IsNearlyZero(1.e-12)?FVector::ZeroVector:FVector(M[V].Inverse().TransformVector(Delta));
                ReconstructionMaximum=FMath::Max(ReconstructionMaximum,(Base[V]+FVector(M[V].TransformVector(Morph.Deltas[V]))-Target[V]).Size());
            }
            if(ReconstructionMaximum>1.e-5){Error=FString::Printf(TEXT("Knee reconstruction mismatch: %s angle=%d error_cm=%.12g"),*Name,Degrees,ReconstructionMaximum);return false;}
            UE_LOG(LogTemp,Display,TEXT("KNEE_CONTINUATION %s angle=%d local_repair_cm=%.9f reconstruction_cm=%.9f"),*Name,Degrees,RepairMaximum,ReconstructionMaximum);
            PreviousTarget=MoveTemp(Target);
            Morph.NormalDeltas=VamMorphNormals::Deltas(I.Vertices,I.Normals,I.Triangles,Morph.Deltas);
            K.Angles.Add(Degrees);K.Morphs.Add(Morph.Name);I.Morphs.Add(MoveTemp(Morph));
            UE_LOG(LogTemp,Display,TEXT("KNEE_OPERATOR %s angle=%d posed_max=%.6f continuation_verified=1"),*Name,Degrees,Maximum);
        }
        P.Knees.Add(MoveTemp(K));
    }
    return true;
}

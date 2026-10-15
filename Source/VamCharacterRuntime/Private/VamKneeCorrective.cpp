#include "VamGluteCorrectiveProfile.h"

TMap<FName,float> VamGluteCorrective::KneeWeights(const UVamGluteCorrectiveProfile& P,const TArray<FTransform>& Pose,const TArray<FTransform>* ShapeReference)
{
    TMap<FName,float> Result;
    for(const auto& K:P.Knees)
    {
        for(FName Name:K.Morphs)if(!Name.IsNone())Result.Add(Name,0);
        if(!Pose.IsValidIndex(K.Bone)||!Pose.IsValidIndex(K.Parent)||K.Angles.Num()!=K.Morphs.Num()||K.Angles.Num()<2)continue;
        const FQuat Local=Pose[K.Bone].GetRelativeTransform(Pose[K.Parent]).GetRotation();
        const FQuat Reference=ShapeReference && ShapeReference->IsValidIndex(K.Bone)?(*ShapeReference)[K.Bone].GetRotation():K.ReferenceRotation;
        const FQuat Delta=(Reference.Inverse()*Local).GetNormalized();
        // Signed twist around source X, cyclically converted to UE Y. A quaternion
        // sign flip must not change the driving angle. Swing is not knee flexion.
        const double Angle=FMath::UnwindDegrees(2*FMath::Atan2(Delta.Y,Delta.W)*180./PI);
        if(!FMath::IsFinite(Angle))continue;
        const double A=FMath::Clamp(Angle,K.Angles[0],K.Angles.Last());
        for(int I=1;I<K.Angles.Num();++I)if(A<=K.Angles[I])
        {
            const double T=(A-K.Angles[I-1])/(K.Angles[I]-K.Angles[I-1]);
            if(!K.Morphs[I-1].IsNone())Result[K.Morphs[I-1]]=1-T;
            if(!K.Morphs[I].IsNone())Result[K.Morphs[I]]=T;break;
        }
    }
    return Result;
}

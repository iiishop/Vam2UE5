#include "VamCharacterDefinition.h"
#include "Engine/SkeletalMesh.h"

bool UVamCharacterDefinition::EvaluateShapeReference(const TMap<FName,float>& Values, TArray<FTransform>& Out, FString& Error) const
{
    const auto* S=Shape.Get(); const auto* Mesh=Body.Get();
    if(!S || !Mesh) { Error=TEXT("Shape reference dependencies unavailable"); return false; }
    const auto& Ref=Mesh->GetRefSkeleton(); Out=S->NeutralLocalBind;
    if(Out.Num()!=Ref.GetRawBoneNum()) { Error=TEXT("Shape reference bone count mismatch"); return false; }
    TArray<FVector> Angles; for(const auto& B:S->SourceOrientations) Angles.Add(B.NeutralDegrees);
    for(const auto& P:Parameters)
    {
        const float* Found=Values.Find(P.Target); const double V=Found?*Found:P.DefaultValue;
        if(!FMath::IsFinite(V)) { Error=TEXT("Nonfinite shape coefficient"); return false; }
        for(const auto& D:P.BoneCenters)
        { if(!Out.IsValidIndex(D.BoneIndex)) { Error=TEXT("Invalid center formula target"); return false; } Out[D.BoneIndex].AddToTranslation(D.LocalTranslation*V); }
        for(const auto& D:P.BoneOrientations)
        { if(!Angles.IsValidIndex(D.BoneIndex)) { Error=TEXT("Invalid orientation formula target"); return false; } Angles[D.BoneIndex]+=D.SourceDegrees*V; }
    }
    if(Angles.IsEmpty()) return true; // Historical schema retains its exact semantics.
    if(Angles.Num()>Out.Num()) { Error=TEXT("Source orientation count exceeds skeleton"); return false; }
    // First reconstruct source world centers with the neutral basis. Offsets are
    // expressed in that basis; rotating them first would introduce a false shift.
    TArray<FTransform> World=Out;
    for(int32 I=0;I<World.Num();++I) if(Ref.GetParentIndex(I)>=0) World[I]=World[I]*World[Ref.GetParentIndex(I)];
    for(int32 I=0;I<Angles.Num();++I)
    {
        const FVector R=Angles[I]*(UE_PI/180.0);
        const FQuat X(FVector(1,0,0),R.X),Y(FVector(0,1,0),R.Y),Z(FVector(0,0,1),R.Z);
        const FQuat Q=S->SourceOrientations[I].bUnityEuler?Y*X*Z:Z*Y*X;
        FQuat UE(Q.Z,Q.X,Q.Y,Q.W); UE.Normalize(); World[I].SetRotation(UE);
    }
    for(int32 I=0;I<Angles.Num();++I) Out[I]=Ref.GetParentIndex(I)<0?World[I]:World[I].GetRelativeTransform(World[Ref.GetParentIndex(I)]);
    return true;
}

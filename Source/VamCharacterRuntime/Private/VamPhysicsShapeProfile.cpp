#include "VamPhysicsShapeProfile.h"

bool UVamPhysicsShapeProfile::Fit(const TMap<FName,float>& Values, const TArray<FTransform>& ReferenceCS,
    TArray<FVamCollisionFit>& Result, FString& Error) const
{
    TArray<FVector> Points=BaselinePoints;
    for(const FVamCollisionMorph& Morph:Morphs)
    {
        const float* Value=Values.Find(Morph.Parameter);
        const float Offset=(Value ? *Value : Morph.Baseline)-Morph.Baseline;
        if(!FMath::IsFinite(Offset)) { Error=TEXT("Non-finite collision shape parameter"); return false; }
        for(const auto& D:Morph.Deltas)
        {
            if(!Points.IsValidIndex(D.Point)) { Error=TEXT("Invalid cooked collision mapping"); return false; }
            Points[D.Point]+=D.Delta*Offset;
        }
    }
    Result=Fits;
    for(auto& F:Result)
    {
        if(!ReferenceCS.IsValidIndex(F.BoneIndex)) { Error=TEXT("Collision bind index mismatch"); return false; }
        if(F.Points.IsEmpty()) continue; // Explicit non-colliding rigid anchor.
        FBox Bounds(ForceInit);
        for(int32 Index:F.Points)
        {
            if(!Points.IsValidIndex(Index)) { Error=TEXT("Invalid collision point index"); return false; }
            Bounds+=F.Rotation.UnrotateVector(ReferenceCS[F.BoneIndex].InverseTransformPosition(Points[Index]));
        }
        const FVector BaseSize=F.BoundsMax-F.BoundsMin;
        if(BaseSize.GetMin()<1.e-5 || !Bounds.IsValid) { Error=TEXT("Degenerate collision fitting region"); return false; }
        const FVector Scale=Bounds.GetSize()/BaseSize;
        if(Scale.ContainsNaN() || Scale.GetMin()<MinimumScale || Scale.GetMax()>MaximumScale)
        { Error=FString::Printf(TEXT("Collision fit for %s outside declared scale domain [%.2f, %.2f]"),*F.Bone.ToString(),MinimumScale,MaximumScale); return false; }
        F.Center=F.Rotation.RotateVector(Bounds.GetCenter()+(F.Rotation.UnrotateVector(F.Center)-(F.BoundsMin+F.BoundsMax)*.5)*Scale);
        F.Radius*=FMath::Max(Scale.X,Scale.Y); F.Length*=Scale.Z;
        if(F.Center.ContainsNaN() || !FMath::IsFinite(F.Radius) || !FMath::IsFinite(F.Length))
        { Error=TEXT("Non-finite fitted primitive"); return false; }
    }
    return true;
}

#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "VamRigProfile.generated.h"

/** Per-skeleton semantic contract. Bone names are data, never assumptions in the solver. */
USTRUCT(BlueprintType)
struct VAMCHARACTERRUNTIME_API FVamRigJoint
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VaM|Rig") FName Semantic;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VaM|Rig") FName Bone;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VaM|Rig") FRotator PreferredBend = FRotator::ZeroRotator;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VaM|Rig") FRotator Minimum = FRotator(-75.f,-75.f,-75.f);
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VaM|Rig") FRotator Maximum = FRotator(75.f,75.f,75.f);
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VaM|Rig") bool bLimitRotation = false;
    /** A mapped body joint may opt out of the on-character pose handles. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VaM|Rig") bool bPoseControl = true;
};

/** Shared policy for the on-character pose controls and the final IK output. */
namespace VamPoseControl
{
    inline bool IsEligible(const FVamRigJoint& Joint)
    {
        if (!Joint.bPoseControl) return false;
        const FString Semantic=Joint.Semantic.ToString();
        if (Semantic==TEXT("pelvis") || Semantic==TEXT("spine") || Semantic==TEXT("spine_upper") ||
            Semantic==TEXT("chest") || Semantic==TEXT("neck") || Semantic==TEXT("head")) return true;
        if (!Semantic.StartsWith(TEXT("left_")) && !Semantic.StartsWith(TEXT("right_"))) return false;
        const FString Part=Semantic.Mid(Semantic.Find(TEXT("_"))+1);
        if (Part==TEXT("clavicle") || Part==TEXT("shoulder") || Part==TEXT("elbow") ||
            Part==TEXT("hand") || Part==TEXT("hip") || Part==TEXT("knee") ||
            Part==TEXT("foot") || Part==TEXT("toe") || Part==TEXT("big_toe")) return true;
        for (const TCHAR* Finger : {TEXT("thumb_"), TEXT("index_"), TEXT("mid_"), TEXT("ring_"), TEXT("pinky_")})
            if (Part.StartsWith(Finger)) return true;
        return false;
    }

    inline void GetLimits(const FVamRigJoint& Joint, FRotator& Minimum, FRotator& Maximum)
    {
        if (Joint.bLimitRotation) { Minimum=Joint.Minimum; Maximum=Joint.Maximum; return; }
        const FString Semantic=Joint.Semantic.ToString();
        // Legacy Stage06 profiles did not serialize finger/toe limits. These finite
        // defaults keep them controllable until a per-skeleton profile is authored.
        const bool bToe=Semantic.EndsWith(TEXT("_toe")) || Semantic.EndsWith(TEXT("_big_toe"));
        const bool bTorso=Semantic==TEXT("pelvis") || Semantic==TEXT("spine") ||
            Semantic==TEXT("spine_upper") || Semantic==TEXT("chest");
        const bool bHead=Semantic==TEXT("neck") || Semantic==TEXT("head");
        if (bTorso || bHead)
        {
            const float Angle=bHead ? 45.f : 35.f;
            Minimum=FRotator(-Angle,-Angle,-Angle);
            Maximum=FRotator(Angle,Angle,Angle);
            return;
        }
        Minimum=bToe ? FRotator(-35.f,-25.f,-25.f) : FRotator(-60.f,-35.f,-35.f);
        Maximum=bToe ? FRotator(35.f,25.f,25.f) : FRotator(60.f,35.f,35.f);
    }

    inline FRotator Clamp(const FVamRigJoint& Joint, const FRotator& Rotation)
    {
        FRotator Minimum,Maximum;
        GetLimits(Joint,Minimum,Maximum);
        const FRotator Normalized=Rotation.GetNormalized();
        return FRotator(
            FMath::Clamp(Normalized.Pitch,FMath::Min(Minimum.Pitch,Maximum.Pitch),FMath::Max(Minimum.Pitch,Maximum.Pitch)),
            FMath::Clamp(Normalized.Yaw,FMath::Min(Minimum.Yaw,Maximum.Yaw),FMath::Max(Minimum.Yaw,Maximum.Yaw)),
            FMath::Clamp(Normalized.Roll,FMath::Min(Minimum.Roll,Maximum.Roll),FMath::Max(Minimum.Roll,Maximum.Roll)));
    }
}

UCLASS(BlueprintType)
class VAMCHARACTERRUNTIME_API UVamRigProfile : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VaM|Rig") TSoftObjectPtr<class USkeleton> Skeleton;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VaM|Rig") TArray<FVamRigJoint> Joints;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VaM|Rig") FName SolverRootSemantic = TEXT("pelvis");
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VaM|Rig") TArray<FName> Effectors;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VaM|Rig") int32 Iterations = 24;
    UFUNCTION(BlueprintPure, Category="VaM|Rig") FName BoneForSemantic(FName Semantic) const
    {
        if (const FVamRigJoint* Joint=Joints.FindByPredicate([Semantic](const FVamRigJoint& J){return J.Semantic==Semantic;})) return Joint->Bone;
        return NAME_None;
    }
};

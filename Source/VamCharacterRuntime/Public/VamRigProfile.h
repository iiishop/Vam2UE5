#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "VamRigProfile.generated.h"

/** Per-skeleton semantic contract. Bone names are data, never assumptions in the solver. */
USTRUCT(BlueprintType)
struct VAMCHARACTERRUNTIME_API FVamRigJoint
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FName Semantic;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FName Bone;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FRotator PreferredBend = FRotator::ZeroRotator;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FRotator Minimum = FRotator(-75.f,-75.f,-75.f);
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FRotator Maximum = FRotator(75.f,75.f,75.f);
    UPROPERTY(EditAnywhere, BlueprintReadOnly) bool bLimitRotation = false;
};

UCLASS(BlueprintType)
class VAMCHARACTERRUNTIME_API UVamRigProfile : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TSoftObjectPtr<class USkeleton> Skeleton;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TArray<FVamRigJoint> Joints;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FName SolverRootSemantic = TEXT("pelvis");
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TArray<FName> Effectors;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 Iterations = 24;
    UFUNCTION(BlueprintPure) FName BoneForSemantic(FName Semantic) const
    {
        if (const FVamRigJoint* Joint=Joints.FindByPredicate([Semantic](const FVamRigJoint& J){return J.Semantic==Semantic;})) return Joint->Bone;
        return NAME_None;
    }
};

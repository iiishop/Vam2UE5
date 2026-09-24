#pragma once
#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "VamRigProfile.h"
#include "VamShapeAnimInstance.generated.h"

/** Evaluates the component's shape reference, including its private inverse bind. */
UCLASS(Transient, Blueprintable)
class VAMCHARACTERRUNTIME_API UVamShapeAnimInstance : public UAnimInstance
{
    GENERATED_BODY()
public:
    /** Temporary local-space pose offsets used by the editor debug tool. */
    void SetDebugBoneOffset(int32 BoneIndex, const FTransform& Offset);
    void ClearDebugBoneOffsets();
    FTransform GetDebugBoneOffset(int32 BoneIndex) const;
    const TMap<int32,FTransform>& GetDebugBoneOffsets() const { return DebugBoneOffsets; }
    void SetActiveBoneOffset(int32 BoneIndex, const FTransform& Offset);
    void ClearActiveBoneOffsets();
    const TMap<int32,FTransform>& GetActiveBoneOffsets() const { return ActiveBoneOffsets; }
    void SetPoseControlRotation(int32 BoneIndex, const FRotator& Rotation);
    void ClearPoseControlRotations();
    const TMap<int32,FRotator>& GetPoseControlRotations() const { return PoseControlRotations; }
    void SetRigProfile(const UVamRigProfile* Profile);
    void SetIKGoal(FName Semantic, const FTransform& WorldGoal);
    void ClearIKGoal(FName Semantic);
    const TArray<FVamRigJoint>& GetRigJoints() const { return RigJoints; }
    const TArray<FName>& GetEffectors() const { return Effectors; }
    FName GetSolverRoot() const { return SolverRoot; }
    int32 GetSolverIterations() const { return SolverIterations; }
    const TMap<FName,FTransform>& GetWorldIKGoals() const { return WorldIKGoals; }
protected:
    virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
    virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* Proxy) override;
private:
    TMap<int32,FTransform> DebugBoneOffsets;
    TMap<int32,FTransform> ActiveBoneOffsets;
    TMap<int32,FRotator> PoseControlRotations;
    TArray<FVamRigJoint> RigJoints;
    TArray<FName> Effectors;
    FName SolverRoot = NAME_None;
    int32 SolverIterations = 24;
    TMap<FName,FTransform> WorldIKGoals;
};

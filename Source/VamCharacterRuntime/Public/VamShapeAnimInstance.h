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
    UPROPERTY(BlueprintReadOnly, Transient, Category="VaM|Timing") double ProducedPoseWorldTimeSeconds=-1;
    UPROPERTY(BlueprintReadOnly, Transient, Category="VaM|Timing") int32 ProducedPoseRevision=0;
    virtual void NativePostEvaluateAnimation() override;
    /** Optional in-place, non-additive base clip. Null explicitly selects RefPose.
        Sampling does not implement gameplay root motion or animation notifies. */
    UFUNCTION(BlueprintCallable, Category="VaM|Animation") bool SetBaseAnimation(class UAnimSequence* Sequence);
    UFUNCTION(BlueprintPure, Category="VaM|Animation") double GetBaseAnimationTime() const { return BaseAnimationTime; }
    UAnimSequence* GetBaseAnimation() const { return BaseAnimation; }
    void AdvanceBaseAnimation(float DeltaSeconds);
    UFUNCTION(BlueprintPure, Category="VaM|Animation") static bool SupportsBaseAnimation(class USkeletalMesh* Mesh, class UAnimSequence* Sequence);
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
    void SetGroundContactGoal(FName Semantic, const FTransform& WorldGoal);
    const TSet<FName>& GetGroundContacts() const { return GroundContacts; }
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
    UPROPERTY(Transient) TObjectPtr<class UAnimSequence> BaseAnimation;
    double BaseAnimationTime=0;
    TMap<int32,FTransform> DebugBoneOffsets;
    TMap<int32,FTransform> ActiveBoneOffsets;
    TMap<int32,FRotator> PoseControlRotations;
    TArray<FVamRigJoint> RigJoints;
    TArray<FName> Effectors;
    FName SolverRoot = NAME_None;
    int32 SolverIterations = 24;
    TMap<FName,FTransform> WorldIKGoals;
    TSet<FName> GroundContacts;
};

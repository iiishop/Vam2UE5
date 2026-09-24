#include "VamShapeAnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimNodeBase.h"
#include "BonePose.h"
#include "Core/PBIKSolver.h"
#include "Core/PBIKBody.h"
#include "Components/SkeletalMeshComponent.h"
class FVamShapeProxy final : public FAnimInstanceProxy
{
public:
    explicit FVamShapeProxy(UAnimInstance* Instance) : FAnimInstanceProxy(Instance) {}
    virtual void PreUpdate(UAnimInstance* Instance, float DeltaSeconds) override
    {
        FAnimInstanceProxy::PreUpdate(Instance,DeltaSeconds);
        Offsets=static_cast<UVamShapeAnimInstance*>(Instance)->GetDebugBoneOffsets();
        ActiveOffsets=static_cast<UVamShapeAnimInstance*>(Instance)->GetActiveBoneOffsets();
        PoseRotations=static_cast<UVamShapeAnimInstance*>(Instance)->GetPoseControlRotations();
        const auto* Anim=static_cast<UVamShapeAnimInstance*>(Instance);
        Joints=Anim->GetRigJoints(); Effectors=Anim->GetEffectors(); SolverRoot=Anim->GetSolverRoot();
        Iterations=Anim->GetSolverIterations(); Goals.Reset();
        if (const auto* Component=Anim->GetSkelMeshComponent())
            for (const auto& Pair:Anim->GetWorldIKGoals())
                Goals.Add(Pair.Key,Pair.Value.GetRelativeTransform(Component->GetComponentTransform()));
    }
    virtual bool Evaluate(FPoseContext& Output) override
    {
        Output.ResetToRefPose();
        const FBoneContainer& Bones=Output.Pose.GetBoneContainer();
        TArray<FQuat> ReferenceRotations;
        ReferenceRotations.Reserve(Output.Pose.GetNumBones());
        for (int32 I=0;I<Output.Pose.GetNumBones();++I)
            ReferenceRotations.Add(Output.Pose[FCompactPoseBoneIndex(I)].GetRotation());
        for (const auto& Pair:ActiveOffsets)
        {
            const FCompactPoseBoneIndex Compact=Bones.GetCompactPoseIndexFromSkeletonPoseIndex(FSkeletonPoseBoneIndex(Pair.Key));
            if (Compact.GetInt()==INDEX_NONE) continue;
            FTransform& Bone=Output.Pose[Compact];
            Bone.AddToTranslation(Pair.Value.GetTranslation());
            Bone.SetRotation((Pair.Value.GetRotation()*Bone.GetRotation()).GetNormalized());
        }
        for (const auto& Pair:PoseRotations)
        {
            const FCompactPoseBoneIndex Compact=Bones.GetCompactPoseIndexFromSkeletonPoseIndex(FSkeletonPoseBoneIndex(Pair.Key));
            if (Compact.GetInt()==INDEX_NONE) continue;
            FTransform& Bone=Output.Pose[Compact];
            Bone.SetRotation((Pair.Value.Quaternion()*Bone.GetRotation()).GetNormalized());
        }
        for (const auto& Pair:Offsets)
        {
            const FCompactPoseBoneIndex Compact=Bones.GetCompactPoseIndexFromSkeletonPoseIndex(FSkeletonPoseBoneIndex(Pair.Key));
            if (Compact.GetInt()==INDEX_NONE) continue;
            FTransform& Bone=Output.Pose[Compact];
            Bone.AddToTranslation(Pair.Value.GetTranslation());
            Bone.SetRotation((Pair.Value.GetRotation()*Bone.GetRotation()).GetNormalized());
        }
        if (!Goals.IsEmpty() && !Joints.IsEmpty()) SolveIK(Output);
        ClampJointRotations(Output,ReferenceRotations);
        return true;
    }
private:
    void ClampJointRotations(FPoseContext& Output, const TArray<FQuat>& ReferenceRotations) const
    {
        const FBoneContainer& Bones=Output.Pose.GetBoneContainer();
        for (const FVamRigJoint& Joint:Joints)
        {
            if (!VamPoseControl::IsEligible(Joint) || Joint.Semantic==SolverRoot) continue;
            const int32 SkeletonIndex=Bones.GetReferenceSkeleton().FindBoneIndex(Joint.Bone);
            if (SkeletonIndex==INDEX_NONE) continue;
            const FCompactPoseBoneIndex Compact=Bones.GetCompactPoseIndexFromSkeletonPoseIndex(FSkeletonPoseBoneIndex(SkeletonIndex));
            if (!ReferenceRotations.IsValidIndex(Compact.GetInt())) continue;
            FTransform& Bone=Output.Pose[Compact];
            const FQuat Delta=(Bone.GetRotation()*ReferenceRotations[Compact.GetInt()].Inverse()).GetNormalized();
            const FRotator Limited=VamPoseControl::Clamp(Joint,Delta.Rotator());
            Bone.SetRotation((Limited.Quaternion()*ReferenceRotations[Compact.GetInt()]).GetNormalized());
        }
    }
    void SolveIK(FPoseContext& Output)
    {
        const FBoneContainer& Bones=Output.Pose.GetBoneContainer();
        const int32 Count=Output.Pose.GetNumBones();
        if (Count<2) return;
        FCSPose<FCompactPose> ComponentPose;
        ComponentPose.InitPose(Output.Pose);
        TArray<FName> Names; Names.Reserve(Count);
        TArray<FTransform> InputCS; InputCS.Reserve(Count);
        for (int32 I=0;I<Count;++I)
        {
            const FCompactPoseBoneIndex Bone(I);
            const int32 SkeletonIndex=Bones.GetSkeletonPoseIndexFromCompactPoseIndex(Bone).GetInt();
            Names.Add(Bones.GetReferenceSkeleton().GetBoneName(SkeletonIndex));
            InputCS.Add(ComponentPose.GetComponentSpaceTransform(Bone));
        }
        const FVamRigJoint* RootJoint=Joints.FindByPredicate([this](const FVamRigJoint& J){return J.Semantic==SolverRoot;});
        if (!RootJoint || !Names.Contains(RootJoint->Bone)) return;
        if (!Solver || CachedNames!=Names || CachedRoot!=RootJoint->Bone)
        {
            Solver=MakeUnique<FPBIKSolver>(); CachedNames=Names; CachedRoot=RootJoint->Bone; EffectorIndices.Reset();
            for (int32 I=0;I<Count;++I)
            {
                const int32 Parent=Bones.GetParentBoneIndex(FCompactPoseBoneIndex(I)).GetInt();
                Solver->AddBone(Names[I],Parent,InputCS[I].GetLocation(),InputCS[I].GetRotation(),Names[I]==CachedRoot);
            }
            for (FName Semantic:Effectors)
                if (const FVamRigJoint* J=Joints.FindByPredicate([Semantic](const FVamRigJoint& V){return V.Semantic==Semantic;}))
                    if (Names.Contains(J->Bone)) EffectorIndices.Add(Semantic,Solver->AddEffector(J->Bone));
            if (!Solver->Initialize()) { Solver.Reset(); return; }
            for (const FVamRigJoint& J:Joints)
            {
                const int32 Index=Names.IndexOfByKey(J.Bone);
                if (Index==INDEX_NONE) continue;
                if (auto* Settings=Solver->GetBoneSettings(Index))
                {
                    Settings->bUsePreferredAngles=!J.PreferredBend.IsNearlyZero();
                    Settings->PreferredAngles=J.PreferredBend;
                    if (J.bLimitRotation || VamPoseControl::IsEligible(J))
                    {
                        FRotator Minimum,Maximum;
                        VamPoseControl::GetLimits(J,Minimum,Maximum);
                        Settings->X=Settings->Y=Settings->Z=PBIK::ELimitType::Limited;
                        Settings->MinX=Minimum.Roll; Settings->MaxX=Maximum.Roll;
                        Settings->MinY=Minimum.Pitch; Settings->MaxY=Maximum.Pitch;
                        Settings->MinZ=Minimum.Yaw; Settings->MaxZ=Maximum.Yaw;
                    }
                }
            }
        }
        for (int32 I=0;I<Count;++I) Solver->SetBoneTransform(I,InputCS[I]);
        for (const auto& Pair:EffectorIndices)
            if (const FVamRigJoint* J=Joints.FindByPredicate([&Pair](const FVamRigJoint& V){return V.Semantic==Pair.Key;}))
            {
                const int32 BoneIndex=Names.IndexOfByKey(J->Bone);
                if (BoneIndex!=INDEX_NONE && Pair.Value>=0)
                {
                    PBIK::FEffectorSettings Neutral;
                    Neutral.PositionAlpha=0; Neutral.RotationAlpha=0; Neutral.StrengthAlpha=0;
                    Solver->SetEffectorGoal(Pair.Value,InputCS[BoneIndex].GetLocation(),InputCS[BoneIndex].GetRotation(),Neutral);
                }
            }
        bool bActive=false;
        for (const auto& Pair:Goals)
            if (Effectors.Contains(Pair.Key))
                if (const FVamRigJoint* J=Joints.FindByPredicate([&Pair](const FVamRigJoint& V){return V.Semantic==Pair.Key;}))
                {
                    const int32* Index=EffectorIndices.Find(Pair.Key);
                    if (Index && *Index>=0)
                    {
                        PBIK::FEffectorSettings Settings;
                        Settings.PositionAlpha=1; Settings.RotationAlpha=1;
                        Solver->SetEffectorGoal(*Index,Pair.Value.GetLocation(),Pair.Value.GetRotation(),Settings);
                        bActive=true;
                    }
                }
        if (!bActive) return;
        FPBIKSolverSettings Settings;
        Settings.Iterations=FMath::Clamp(Iterations,1,100);
        Settings.RootBehavior=EPBIKRootBehavior::PinToInput;
        Settings.bAllowStretch=false;
        Solver->Solve(Settings);
        TArray<FTransform> Solved; Solved.SetNum(Count);
        for (int32 I=0;I<Count;++I) Solver->GetBoneGlobalTransform(I,Solved[I]);
        for (int32 I=0;I<Count;++I)
        {
            const FCompactPoseBoneIndex Bone(I);
            const int32 Parent=Bones.GetParentBoneIndex(Bone).GetInt();
            Output.Pose[Bone]=Parent==INDEX_NONE ? Solved[I] : Solved[I].GetRelativeTransform(Solved[Parent]);
            Output.Pose[Bone].NormalizeRotation();
        }
    }
    TMap<int32,FTransform> Offsets;
    TMap<int32,FTransform> ActiveOffsets;
    TMap<int32,FRotator> PoseRotations;
    TArray<FVamRigJoint> Joints;
    TArray<FName> Effectors;
    FName SolverRoot;
    int32 Iterations=24;
    TMap<FName,FTransform> Goals;
    TUniquePtr<FPBIKSolver> Solver;
    TMap<FName,int32> EffectorIndices;
    TArray<FName> CachedNames;
    FName CachedRoot;
};
FAnimInstanceProxy* UVamShapeAnimInstance::CreateAnimInstanceProxy() { return new FVamShapeProxy(this); }
void UVamShapeAnimInstance::DestroyAnimInstanceProxy(FAnimInstanceProxy* Proxy) { delete Proxy; }
void UVamShapeAnimInstance::SetDebugBoneOffset(int32 BoneIndex, const FTransform& Offset)
{
    if (Offset.Equals(FTransform::Identity)) DebugBoneOffsets.Remove(BoneIndex);
    else DebugBoneOffsets.Add(BoneIndex,Offset);
}
void UVamShapeAnimInstance::ClearDebugBoneOffsets() { DebugBoneOffsets.Reset(); }
void UVamShapeAnimInstance::SetActiveBoneOffset(int32 BoneIndex, const FTransform& Offset)
{
    if (Offset.Equals(FTransform::Identity)) ActiveBoneOffsets.Remove(BoneIndex);
    else ActiveBoneOffsets.Add(BoneIndex,Offset);
}
void UVamShapeAnimInstance::ClearActiveBoneOffsets() { ActiveBoneOffsets.Reset(); }
void UVamShapeAnimInstance::SetPoseControlRotation(int32 BoneIndex, const FRotator& Rotation)
{
    if (Rotation.IsNearlyZero()) PoseControlRotations.Remove(BoneIndex);
    else PoseControlRotations.Add(BoneIndex,Rotation);
}
void UVamShapeAnimInstance::ClearPoseControlRotations() { PoseControlRotations.Reset(); }
FTransform UVamShapeAnimInstance::GetDebugBoneOffset(int32 BoneIndex) const
{
    if (const FTransform* Found=DebugBoneOffsets.Find(BoneIndex)) return *Found;
    return FTransform::Identity;
}
void UVamShapeAnimInstance::SetRigProfile(const UVamRigProfile* Profile)
{
    RigJoints=Profile ? Profile->Joints : TArray<FVamRigJoint>();
    Effectors=Profile ? Profile->Effectors : TArray<FName>();
    SolverRoot=Profile ? Profile->SolverRootSemantic : NAME_None;
    SolverIterations=Profile ? Profile->Iterations : 24;
}
void UVamShapeAnimInstance::SetIKGoal(FName Semantic, const FTransform& WorldGoal)
{
    if (WorldGoal.IsValid()) WorldIKGoals.Add(Semantic,WorldGoal);
}
void UVamShapeAnimInstance::ClearIKGoal(FName Semantic) { WorldIKGoals.Remove(Semantic); }

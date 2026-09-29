#include "VamLegSkeletalMeshComponent.h"
#include "VamCharacterComponent.h"
#include "VamMotionComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "DrawDebugHelpers.h"

void UVamLegSkeletalMeshComponent::ResetLegTuning()
{
    ThighAmplitude=CalfAmplitude=3;LegSupport=.85;LegDamping=1;ThighDamping=.3;CalfDamping=.4;ResetLegJiggle();
}
void UVamLegSkeletalMeshComponent::ResetLegJiggle(){for(auto& S:LegSolvers) S.Reset();LegLastTime=-1;}
void UVamLegSkeletalMeshComponent::UpdateLegShape(const TMap<FName,float>& Values,TArray<FTransform>& Reference)
{
    if(!LegProfile || !LegProfile->IsValidProfile()) return;LegRest=LegProfile->Segments;bLegShapeRebase=true;
    auto CS=Reference;const auto& Ref=GetSkeletalMeshAsset()->GetRefSkeleton();for(int32 B=0;B<CS.Num();++B) if(Ref.GetParentIndex(B)>=0) CS[B]=CS[B]*CS[Ref.GetParentIndex(B)];
    for(auto& S:LegRest)
    {
        double VolumeLogChange=0;
        for(const auto& Response:S.ShapeResponses)
        {
            const float* Value=Values.Find(Response.Parameter);const double Weight=Value?*Value-Response.DefaultValue:0;VolumeLogChange+=Weight*Response.LogVolume;
            for(int32 N=0;N<5;++N) S.Dynamics.Nodes[N].Rest+=Response.RestDeltas[N]*Weight;
        }
        const double VolumeScale=FMath::Exp(FMath::Clamp(VolumeLogChange,-2.,2.)),RadiusScale=FMath::Pow(VolumeScale,1./3.);
        S.EffectiveVolumeCm3*=VolumeScale;S.Dynamics.MassKg*=VolumeScale;S.Radius*=RadiusScale;
        for(auto& N:S.Dynamics.Nodes){N.MassKg*=VolumeScale;N.Support*=VolumeScale/RadiusScale;N.PositiveTravel*=RadiusScale;N.NegativeTravel*=RadiusScale;Reference[N.BoneIndex]=FTransform(N.Rest);}
        S.Dynamics.COM=FVector::ZeroVector;
        const FTransform Anchor=S.AnchorLocal*CS[S.bCalf?S.Shin:S.Thigh];
        for(auto& N:S.Dynamics.Nodes)
        {
            N.COM=N.Rest;S.Dynamics.COM+=N.COM*N.MassKg;N.PelvisPoint=FVector(0,0,N.Rest.Z);
            N.ThighPointLocal=CS[S.bCalf?S.Foot:S.Shin].InverseTransformPosition(Anchor.TransformPosition(N.Rest));
        }
        S.Dynamics.COM/=S.Dynamics.MassKg;
        S.Dynamics.Dimensions.X*=RadiusScale;S.Dynamics.Dimensions.Y*=RadiusScale;
        for(auto& Edge:S.Dynamics.Couplings) Edge.Stiffness*=VolumeScale/RadiusScale;
        S.JointRest={CS[S.Thigh].GetRelativeTransform(CS[S.Pelvis]),CS[S.Shin].GetRelativeTransform(CS[S.Thigh]),CS[S.Foot].GetRelativeTransform(CS[S.Shin])};
        S.ParentRestRotations={CS[S.Pelvis].GetRotation(),CS[S.Thigh].GetRotation(),CS[S.Shin].GetRotation()};Reference[S.AnchorBone]=S.AnchorLocal;
    }
}
void UVamLegSkeletalMeshComponent::FinalizeBoneTransform()
{
    if(LegProfile && LegProfile->IsValidProfile() && GetWorld())
    {
        if(!LegIntegration || IntegrationSource!=LegProfile)
        {
            if(IntegrationSource && IntegrationSource!=LegProfile) LegRest.Reset();
            ResetLegJiggle();
            LegIntegration=DuplicateObject<UVamGluteJiggleProfile>(LegProfile->Integration,this,MakeUniqueObjectName(this,UVamGluteJiggleProfile::StaticClass()));
            const double TimeBudget=LegIntegration->FixedStep*LegIntegration->MaxSubsteps;
            LegIntegration->FixedStep=1./240.;
            LegIntegration->MaxSubsteps=FMath::Clamp(FMath::CeilToInt(TimeBudget/LegIntegration->FixedStep),32,128);
            IntegrationSource=LegProfile;
        }
        if(LegRest.Num()!=4) LegRest=LegProfile->Segments;
        auto& Pose=GetEditableComponentSpaceTransforms();const auto PrimaryPose=Pose;
        const double Now=GetWorld()->GetTimeSeconds(),Dt=LegLastTime<0?0:Now-LegLastTime;
        const auto* Motion=GetOwner()->FindComponentByClass<UVamMotionComponent>();const int32 Revision=Motion?Motion->GetClock().TeleportRevision:0;
        const bool Paused=GetWorld()->IsPaused() || (Motion && Motion->GetClock().bPaused);
        for(int32 I=0;I<4;++I)
        {
            const auto& S=LegRest[I];if(!Pose.IsValidIndex(S.AnchorBone) || !Pose.IsValidIndex(S.Foot)) continue;
            FVector2D SideAngles;LegAngles[I]=VamLegDynamics::JointAngles(S,PrimaryPose,SideAngles);
            const auto Dynamics=VamLegDynamics::Evaluate(*LegProfile,S,LegAngles[I],SideAngles,LegTension[I]);
            const FTransform Anchor=S.AnchorLocal*PrimaryPose[S.bCalf?S.Shin:S.Thigh];Pose[S.AnchorBone]=Anchor;
            FVamGluteMotion Input;Input.Pelvis=Anchor*GetComponentTransform();Input.Thigh=PrimaryPose[S.bCalf?S.Foot:S.Shin]*GetComponentTransform();
            FVamGluteTuning Tuning;Tuning.Support=LegSupport;Tuning.Damping=LegDamping*(S.bCalf?CalfDamping:ThighDamping);
            if(bLegJiggleEnabled && GetWorld()->IsGameWorld()) LegSolvers[I].Advance(*LegIntegration,Dynamics,Input,Dt,FVector(0,0,GetWorld()->GetGravityZ()),Tuning,Revision!=LegLastTeleport || bLegWasEnabled!=bLegJiggleEnabled,Paused,bLegShapeRebase);
            else LegSolvers[I].Reset();
            const double Value=S.bCalf?CalfAmplitude:ThighAmplitude,Amplitude=FMath::Clamp(FMath::IsFinite(Value)?Value:3.,0.,10.);
            for(int32 N=0;N<5;++N)
            {
                const auto& Node=Dynamics.Nodes[N];FVector Offset=bLegJiggleEnabled?LegSolvers[I].Nodes[N].Displacement*Amplitude:FVector::ZeroVector;
                // Bound amplified output smoothly in segment-local geometry. This also
                // prevents the reference-gravity residual becoming a large static bulge.
                const FVector Envelope(FMath::Max(.1,S.Radius*.35),FMath::Max(.1,S.Radius*.35),FMath::Max(.1,S.Radius*.18));
                Offset/=FMath::Sqrt(1+(Offset/Envelope).SizeSquared());
                Pose[Node.BoneIndex]=FTransform(Node.Rest+Offset)*Anchor;
                const FVector Rest=Input.Pelvis.TransformPosition(Node.Rest),Position=Input.Pelvis.TransformPosition(Node.Rest+Offset);
                if(bShowLegNodes){DrawDebugPoint(GetWorld(),Position,7,FColor::Orange,false,0);DrawDebugLine(GetWorld(),Rest,Position,FColor::Magenta,false,0);}
                if(bShowLegTension) DrawDebugString(GetWorld(),Position,FString::Printf(TEXT("%s passive %.3f"),*Node.Semantic.ToString(),LegTension[I][N]),nullptr,FColor::White,0);
            }
            if(bShowLegRegion) for(int32 V=0;V<S.RegionWeights.Num();V+=3) if(S.RegionWeights[V]>.02) DrawDebugPoint(GetWorld(),Input.Pelvis.TransformPosition(S.RegionPoints[V]),3,FLinearColor(S.RegionWeights[V],.1f,1-S.RegionWeights[V]).ToFColor(false),false,0);
        }
        LegLastTime=Now;LegLastTeleport=Revision;bLegWasEnabled=bLegJiggleEnabled;bLegShapeRebase=false;
    }
    // Helpers are disjoint. Hip and Breast retain their existing final-pose writes.
    Super::FinalizeBoneTransform();
}
FString UVamLegSkeletalMeshComponent::LegDiagnostics() const
{
    if(!LegProfile) return TEXT("Leg Jiggle profile absent: Generate / Upgrade Runtime to a new BP.");
    FString Out=LegProfile->Algorithm+TEXT(" | ")+LegProfile->GetPathName()+TEXT("\nPose tension is passive length-based approximation, not active muscle activation.\n");
    Out+=FString::Printf(TEXT("240 Hz | support %.2f | thigh damping %.2f calf damping %.2f (lower = longer ring-down)\n"),LegSupport,LegDamping*ThighDamping,LegDamping*CalfDamping);
    for(int32 I=0;I<LegRest.Num();++I)
    {
        const auto& S=LegRest[I];const auto& Solver=LegSolvers[I];Out+=FString::Printf(TEXT("%s hip/knee/ankle %s deg | volume %.2f cm3 mass %.3f kg | gravity residual %s\nsteps %d limits %d solver %.2f us\n"),*S.Name.ToString(),*(LegAngles[I]*(180./PI)).ToCompactString(),S.EffectiveVolumeCm3,S.Dynamics.MassKg,*Solver.GravityResidualLocal.ToCompactString(),Solver.LastSteps,Solver.LimitCorrections,Solver.LastCostMicroseconds);
        for(int32 N=0;N<LegTension[I].Num();++N) Out+=FString::Printf(TEXT("%s tension %.3f solver offset %s cm\n"),*S.Dynamics.Nodes[N].Semantic.ToString(),LegTension[I][N],*Solver.Nodes[N].Displacement.ToCompactString());
    }
    return Out;
}
void UVamLegSkeletalMeshComponent::LegPoseCommand(FName Command)
{
    auto* Character=GetOwner()?GetOwner()->FindComponentByClass<UVamCharacterComponent>():nullptr;
    if(!Character || !LegProfile || !GetSkeletalMeshAsset()) return;
    // Pose presets are an explicit isolated debug pose, not additive walk offsets.
    GluteMotionCommand(TEXT("Reset"));
    Character->ResetDebugBoneOffsets();Character->ResetPoseControlRotations();
    Character->SetFootLocked(TEXT("left_foot"),false);Character->SetFootLocked(TEXT("right_foot"),false);
    Character->ClearIKGoal(TEXT("pelvis"));
    const auto& Ref=GetSkeletalMeshAsset()->GetRefSkeleton();
    auto Local=Character->GetShapeReferencePose();if(Local.Num()!=Ref.GetRawBoneNum()) Local=Ref.GetRefBonePose();
    auto Neutral=Local;for(int32 B=0;B<Neutral.Num();++B) if(Ref.GetParentIndex(B)>=0) Neutral[B]=Neutral[B]*Neutral[Ref.GetParentIndex(B)];
    const bool Crouch=Command==TEXT("Crouch");
    for(const auto& S:LegProfile->Segments) if(!S.bCalf)
    {
        double Hip=0,Knee=0,Ankle=0;
        // Single-joint probes use the left leg; the right leg remains the support leg.
        if(S.Side==0)
        {
            if(Command==TEXT("Hip Flexion")) Hip=45;
            if(Command==TEXT("Knee Flexion")) Knee=60;
            if(Command==TEXT("Dorsiflexion")) {Hip=20;Ankle=15;}
            if(Command==TEXT("Plantarflexion")) {Hip=20;Ankle=-20;}
        }
        // Equal hip/knee angles keep the shins vertical and the feet level.
        if(Crouch){Hip=45;Knee=45;Ankle=0;}
        const int32 Bones[]={S.Thigh,S.Shin,S.Foot};const double Angles[]={-Hip,Knee,-Ankle};
        for(int32 J=0;J<3;++J)
        {
            const int32 Parent=Ref.GetParentIndex(Bones[J]);
            const FVector Axis=Parent>=0?Neutral[Parent].GetRotation().UnrotateVector(S.LateralAxis):S.LateralAxis;
            const FQuat Delta(Axis,FMath::DegreesToRadians(Angles[J]));
            Character->SetDebugBoneOffset(Bones[J],FTransform(Delta));
            Local[Bones[J]].SetRotation((Delta*Local[Bones[J]].GetRotation()).GetNormalized());
        }
    }
    if(Crouch)
    {
        auto Posed=Local;for(int32 B=0;B<Posed.Num();++B) if(Ref.GetParentIndex(B)>=0) Posed[B]=Posed[B]*Posed[Ref.GetParentIndex(B)];
        FVector Shift=FVector::ZeroVector;int32 Count=0;
        for(const auto& S:LegProfile->Segments) if(!S.bCalf){Shift+=Neutral[S.Foot].GetLocation()-Posed[S.Foot].GetLocation();++Count;}
        // Root translation preserves the original foot locations without moving the Actor.
        if(Count) Character->SetDebugBoneOffset(0,FTransform(Shift/Count));
    }
    ResetLegJiggle();
}

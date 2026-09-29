#include "VamGluteSkeletalMeshComponent.h"
#include "VamCharacterComponent.h"
#include "VamMotionComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "DrawDebugHelpers.h"

FVamGluteTuning UVamGluteSkeletalMeshComponent::GetGluteTuning() const
{
    FVamGluteTuning T;T.Support=GluteSupport;T.Damping=GluteDamping;T.Mobility=GluteMobility;T.InternalCoupling=GluteInternalCoupling;T.MassScale=GluteMassScale;return T;
}
void UVamGluteSkeletalMeshComponent::ResetGluteJiggle()
{
    for(auto& S:GluteSolvers) S.Reset();GluteLastTime=-1;
}
void UVamGluteSkeletalMeshComponent::ApplyGluteJiggle()
{
    if(!bGluteEnabled) { ResetGluteJiggle();return; }
    if(!GluteJiggleProfile || !GetWorld() || !GetWorld()->IsGameWorld() || GluteRest.Num()!=2 || GluteStates.Num()!=2 || HipPoseState.Sides.Num()!=2) return;
    const double Now=GetWorld()->GetTimeSeconds(),Dt=GluteLastTime<0?0:Now-GluteLastTime;
    const auto* Motion=GetOwner()->FindComponentByClass<UVamMotionComponent>();const int32 Teleport=Motion?Motion->GetClock().TeleportRevision:0;
    const bool Paused=GetWorld()->IsPaused() || (Motion && Motion->GetClock().bPaused);
    const bool Reset=Teleport!=GluteLastTeleport || bGluteWasEnabled!=bGluteJiggleEnabled;
    auto& Pose=GetEditableComponentSpaceTransforms();GluteDynamics.SetNum(2);
    const double Amplitude=FMath::Clamp(FMath::IsFinite(GluteAmplitude)?GluteAmplitude:3.,0.,10.);
    for(int32 I=0;I<2;++I)
    {
        const auto& S=GluteRest[I];const auto& Structural=GluteStates[I];auto& Solver=GluteSolvers[I];auto& R=GluteDynamics[I];R=VamGluteDynamics::Calibrate(*GluteJiggleProfile,S,Structural);
        FVamGluteMotion M;const FTransform AnchorCS=S.AnchorLocal*HipPoseState.PelvisComponent;
        M.Pelvis=AnchorCS*GetComponentTransform();M.Thigh=(I==0?HipPoseState.LeftFemurComponent:HipPoseState.RightFemurComponent)*GetComponentTransform();
        const FVector Gravity=bGluteGravityOverride?GluteDebugGravityWorld:FVector(0,0,GetWorld()->GetGravityZ());
        if(bGluteJiggleEnabled) Solver.Advance(*GluteJiggleProfile,R,M,Dt,Gravity,GetGluteTuning(),Reset,Paused,bGluteShapeRebase);
        else Solver.Reset();
        for(int32 N=0;N<5;++N)
        {
            const auto& Node=R.Nodes[N];const auto& Dynamic=Solver.Nodes[N];auto Local=Structural.Regions[N].Transform;
            const FVector Offset=bGluteJiggleEnabled?Dynamic.Displacement*Amplitude:FVector::ZeroVector;Local.AddToTranslation(Offset);Pose[Node.BoneIndex]=Local*AnchorCS;
            const FVector Rest=M.Pelvis.TransformPosition(Node.Rest),Position=M.Pelvis.TransformPosition(Local.GetLocation());
            if(bShowGluteDynamicNodes) DrawDebugPoint(GetWorld(),Position,7,FColor::Orange,false,0);
            if(bShowGluteRestDynamic) { DrawDebugPoint(GetWorld(),Rest,5,FColor::Cyan,false,0);DrawDebugLine(GetWorld(),Rest,Position,FColor::Magenta,false,0,0,1); }
            if(bShowGluteDynamicPelvis) DrawDebugLine(GetWorld(),M.Pelvis.TransformPosition(Node.PelvisPoint),Position,FColor::Green,false,0);
            if(bShowGluteDynamicThigh) DrawDebugLine(GetWorld(),M.Thigh.TransformPosition(Node.ThighPointLocal),Position,FColor::Yellow,false,0);
            if(bShowGluteVelocity) DrawDebugLine(GetWorld(),Position,Position+M.Pelvis.TransformVectorNoScale(Dynamic.RelativeVelocity)*(.05*Amplitude),FColor::Red,false,0);
        }
    }
    GluteLastTime=Now;GluteLastTeleport=Teleport;bGluteWasEnabled=bGluteJiggleEnabled;bGluteShapeRebase=false;
}
FString UVamGluteSkeletalMeshComponent::GluteJiggleDiagnostics() const
{
    if(!GluteJiggleProfile) return TEXT("G1 profile absent: Upgrade Runtime to a new output.");
    FString Text=GluteJiggleProfile->Algorithm+TEXT(" | ")+GluteJiggleProfile->GetPathName()+TEXT("\n");
    Text+=FString::Printf(TEXT("Amplitude %.2fx：最终 helper 动态位移倍率；下方 offset / travel 为未放大的 solver 状态。\n"),GluteAmplitude);
    if(GluteJiggleProfile->SchemaVersion<2) Text+=TEXT("旧 G1 重力契约：Upgrade Runtime required。当前资产保留旧行为；仅更新 DLL 不会升级 Profile。\n");
    Text+=bGluteGravityOverride?TEXT("Gravity override：仅当前人物 Glute solver；不修改场景、Breast 或项目重力。\n"):TEXT("Gravity Default：使用当前 World gravity。\n");
    if(GluteProfile && GluteProfile->Algorithm!=TEXT("glute-structure-g05-surface-v3"))
        Text+=TEXT("旧版臀部蒙皮传递标定：请 Upgrade Runtime 生成新人物，并替换当前 BP；仅更新插件不会更新旧蒙皮。\n");
    Text+=GluteJiggleProfile->bNormalizedAttachmentDamping?TEXT("Damping = 双支承合成阻尼倍率（参考标定）；骨节点位移与表面位移不同，请用表面对比。\n"):TEXT("Legacy: attachment damping ratios add.\n");
    if(GetWorld() && !GetWorld()->IsGameWorld()) Text+=TEXT("Jiggle 需要 Play / Simulate；静态编辑视口不推进动态求解。\n");
    for(int32 I=0;I<GluteDynamics.Num();++I)
    {
        const auto& R=GluteDynamics[I];const auto& S=GluteSolvers[I];Text+=FString::Printf(TEXT("%s v %s a %s omega %s alpha %s\nsteps %d dropped %d limits %d sleep %d solver %.2f us | coupling %.2f\n"),*R.Side.ToString(),*S.LinearVelocity.ToCompactString(),*S.LinearAcceleration.ToCompactString(),*S.Omega.ToCompactString(),*S.Alpha.ToCompactString(),S.LastSteps,S.DroppedSteps,S.LimitCorrections,S.bSleeping,S.LastCostMicroseconds,GluteInternalCoupling);
        for(int32 N=0;N<5;++N)
        {
            const auto& D=S.Nodes[N];const auto& A=R.Nodes[N];Text+=FString::Printf(TEXT("%s mass %.5f kg rest %s offset %s v %s\nP/T %.3f/%.3f thigh v %s tension %.4f K %s C %s travel %s\n"),*A.Semantic.ToString(),D.Mass,*A.Rest.ToCompactString(),*D.Displacement.ToCompactString(),*D.RelativeVelocity.ToCompactString(),A.PelvisAttachment,A.ThighAttachment,*D.ThighTargetVelocity.ToCompactString(),GluteStates[I].Regions[N].Tension,*D.Support.ToCompactString(),*D.Damping.ToCompactString(),*D.Travel.ToCompactString());
        }
        Text+=FString::Printf(TEXT("Gravity world %s | current local %s\nreference local %s | residual local %s\nGravityForce %s | ReferencePreload %s kg cm/s² | EffectiveGravityMagnitude %.6f cm/s²\n"),*S.WorldGravity.ToCompactString(),*S.CurrentGravityLocal.ToCompactString(),*S.ReferenceGravityLocal.ToCompactString(),*S.GravityResidualLocal.ToCompactString(),*S.GravityForce.ToCompactString(),*S.GravityPreload.ToCompactString(),S.GravityResidualLocal.Size());
    }
    return Text;
}
void UVamGluteSkeletalMeshComponent::GluteMotionCommand(FName Command)
{
    if(Command==TEXT("Gravity Default")) { bGluteGravityOverride=false;return; }
    if(Command==TEXT("Gravity Zero") || Command==TEXT("Gravity Half") || Command==TEXT("Gravity Double"))
    {
        const FVector Reference=GluteJiggleProfile && GluteJiggleProfile->SchemaVersion>=2?GluteJiggleProfile->AuthoredGravityWorld:FVector(0,0,-980);
        GluteDebugGravityWorld=Reference*(Command==TEXT("Gravity Zero")?0.:Command==TEXT("Gravity Half")?.5:2.);bGluteGravityOverride=true;return;
    }
    if(Command==TEXT("Rotate Character 90 Pitch") || Command==TEXT("Rotate Character 90 Roll") || Command==TEXT("Reset Orientation"))
    {
        if(!GetOwner()) return;
        if(!bGluteOrientationCaptured) { GluteOriginalOrientation=GetOwner()->GetActorQuat();bGluteOrientationCaptured=true; }
        BreastMotionCommand(TEXT("Reset"));
        GluteOrientationStart=GetOwner()->GetActorQuat();GluteOrientationTarget=GluteOriginalOrientation;
        if(Command!=TEXT("Reset Orientation")) GluteOrientationTarget=FQuat(Command==TEXT("Rotate Character 90 Pitch")?FVector::YAxisVector:FVector::XAxisVector,PI/2)*GluteOriginalOrientation;
        GluteOrientationTime=0;bGluteOrientationMoving=true;return;
    }
    if(Command==TEXT("Walk Cycle / Alternating Thigh Swing")) { bGluteWalking=true;GluteWalkTime=0;return; }
    if(Command==TEXT("Reset")) { bGluteWalking=false;GlutePoseCommand(TEXT("Reset"));ResetGluteJiggle();BreastMotionCommand(TEXT("Reset"));return; }
    if(Command==TEXT("Lateral Accelerate")) Command=TEXT("Lateral accelerate");
    if(Command==TEXT("Smooth Turn")) Command=TEXT("Smooth Rotate Start");
    if(Command==TEXT("Continuous Turn")) Command=TEXT("Continuous Rotate");
    if(Command==TEXT("Smooth Turn Stop")) Command=TEXT("Smooth Rotate Stop");
    // Existing smooth trajectories move the actor; neither solver reads commands.
    BreastMotionCommand(Command);
}
void UVamGluteSkeletalMeshComponent::TickComponent(float Dt,ELevelTick Type,FActorComponentTickFunction* Tick)
{
    const auto* Motion=GetOwner()?GetOwner()->FindComponentByClass<UVamMotionComponent>():nullptr;
    if(bGluteOrientationMoving && GetOwner() && GetWorld() && GetWorld()->IsGameWorld() && !GetWorld()->IsPaused() && !(Motion && Motion->GetClock().bPaused))
    {
        GluteOrientationTime+=Dt;const double U=FMath::Clamp(GluteOrientationTime,0.,1.);
        GetOwner()->SetActorRotation(FQuat::Slerp(GluteOrientationStart,GluteOrientationTarget,U*U*(3-2*U)).GetNormalized());
        if(U>=1) bGluteOrientationMoving=false;
    }
    if(bGluteWalking && GluteProfile && GetWorld() && GetWorld()->IsGameWorld() && !GetWorld()->IsPaused() && !(Motion && Motion->GetClock().bPaused))
    {
        GluteWalkTime+=Dt;auto* C=GetOwner()->FindComponentByClass<UVamCharacterComponent>();
        if(C) for(int32 I=0;I<2;++I)
        {
            const auto& S=GluteProfile->Sides[I];const double Ramp=FMath::SmoothStep(0.,.5,GluteWalkTime),Angle=FMath::DegreesToRadians(25.)*FMath::Sin(GluteWalkTime*2*PI+(I?PI:0))*Ramp;
            C->SetDebugBoneOffset(S.ThighBone,FTransform(FQuat(S.AnchorLocal.TransformVectorNoScale(FVector::YAxisVector),Angle)));
        }
    }
    Super::TickComponent(Dt,Type,Tick);
}

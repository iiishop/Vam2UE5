#include "VamBreastSkeletalMeshComponent.h"
#include "VamMotionComponent.h"
#include "Engine/World.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/Actor.h"
#include "DrawDebugHelpers.h"

FVamBreastTuning UVamBreastSkeletalMeshComponent::GetBreastTuning() const
{
    FVamBreastTuning Result;Result.FrequencyScale=FrequencyScale;Result.DampingScale=DampingScale;
    Result.TravelScale=TravelScale;Result.CouplingScale=CouplingScale;return Result;
}
void UVamBreastSkeletalMeshComponent::ApplyBreastTuningPreset(FName Preset)
{
    if(Preset!=TEXT("Profile") && Preset!=TEXT("Lively") && Preset!=TEXT("Exaggerated")) return;
    FrequencyScale=DampingScale=TravelScale=FVector(1);CouplingScale=Softness=1;
    if(Preset==TEXT("Lively"))
    { FrequencyScale=FVector(.8,.75,.7);DampingScale=FVector(.65);TravelScale=FVector(1.4);CouplingScale=.7; }
    else if(Preset==TEXT("Exaggerated"))
    { FrequencyScale=FVector(.6,.55,.5);DampingScale=FVector(.4);TravelScale=FVector(2);CouplingScale=.45; }
    DensityOverrideKgPerCm3=0;
    if(BreastProfile) for(auto& R:RestSides) R.MassKg=R.EffectiveVolumeCm3*BreastProfile->DensityKgPerCm3;
    ResetBreastJiggle();
}

void UVamBreastSkeletalMeshComponent::ResetBreastJiggle()
{
    for(auto& S:Solvers) S.Reset();
    LastTime=-1;bRebase=true;
}
void UVamBreastSkeletalMeshComponent::UpdateBreastShape(const TMap<FName,float>& Values,TArray<FTransform>& Reference,bool Committed)
{
    if(!BreastProfile || !BreastProfile->IsValidProfile()) return;
    const auto Previous=RestSides;
    RestSides=BreastProfile->Sides;
    for(int32 Side=0;Side<RestSides.Num();++Side)
    {
        auto& R=RestSides[Side];double LogScale=0;
        for(const auto& Response:R.ShapeResponses)
        {
            const float* Value=Values.Find(Response.Parameter);
            const double Delta=(Value ? *Value : Response.DefaultValue)-Response.DefaultValue;
            R.AnchorLocal.AddToTranslation(Response.AnchorTranslationDelta*Delta);
            R.EffectiveDepthCm+=Response.DepthDelta*Delta;R.SupportAreaCm2+=Response.SupportAreaDelta*Delta;
            LogScale+=Delta*Response.LogVolumeSlope;R.COM+=Response.COMDelta*Delta;
            R.EffectiveRadiusCm+=Response.RadiusDelta*Delta;
            for(int32 N=0;N<R.Nodes.Num();++N) if(Response.NodeDeltas.IsValidIndex(N)) R.Nodes[N].Rest+=Response.NodeDeltas[N]*Delta;
        }
        const double Ratio=FMath::Exp(FMath::Clamp(LogScale,-3.,3.));
        R.EffectiveVolumeCm3*=Ratio;R.MassKg=R.EffectiveVolumeCm3*(DensityOverrideKgPerCm3>0 ? DensityOverrideKgPerCm3 : BreastProfile->DensityKgPerCm3);
        R.EffectiveRadiusCm=FMath::Max(.1,R.EffectiveRadiusCm);
        R.EffectiveDepthCm=FMath::Max(.01,R.EffectiveDepthCm);R.SupportAreaCm2=FMath::Max(.01,R.SupportAreaCm2);
        if(Reference.IsValidIndex(R.AnchorBone)) Reference[R.AnchorBone]=R.AnchorLocal;
        for(const auto& N:R.Nodes) if(Reference.IsValidIndex(N.BoneIndex)) Reference[N.BoneIndex].SetTranslation(N.Rest);
        if(Previous.IsValidIndex(Side) && Solvers.IsValidIndex(Side) &&
            FMath::Abs(R.EffectiveVolumeCm3/Previous[Side].EffectiveVolumeCm3-1)>BreastProfile->LargeShapeChangeRatio) Solvers[Side].Reset();
    }
    // Shape transactions rebase motion history, never differentiate the user's morph edits.
    for(auto& S:Solvers) { S.Samples=0;S.Accumulator=0; }
}
void UVamBreastSkeletalMeshComponent::FinalizeBoneTransform()
{
    if(BreastProfile && BreastProfile->IsValidProfile() && GetWorld() && GetWorld()->IsGameWorld())
    {
        if(RestSides.IsEmpty()) RestSides=BreastProfile->Sides;
        Solvers.SetNum(RestSides.Num());
        const double Now=GetWorld()->GetTimeSeconds();
        const double Dt=LastTime<0 ? 0 : Now-LastTime;
        const auto* Motion=GetOwner()->FindComponentByClass<UVamMotionComponent>();
        const int32 Teleport=Motion ? Motion->GetClock().TeleportRevision : 0;
        const bool Paused=GetWorld()->IsPaused() || (Motion && Motion->GetClock().bPaused);
        if(Paused) for(auto& S:Solvers) { S.Samples=0;S.Accumulator=0; }
        const bool Reset=bRebase || Teleport!=LastTeleport || bWasEnabled!=bJiggleEnabled;
        auto& Pose=GetEditableComponentSpaceTransforms();
        for(int32 Side=0;Side<RestSides.Num();++Side)
        {
            const auto& R=RestSides[Side];auto& Solver=Solvers[Side];
            if(!Pose.IsValidIndex(R.ChestBone) || !Pose.IsValidIndex(R.AnchorBone)) continue;
            const FTransform AnchorCS=R.AnchorLocal*Pose[R.ChestBone];
            const FTransform World=AnchorCS*GetComponentTransform();
            Pose[R.AnchorBone]=AnchorCS;
            if(bJiggleEnabled && Dt>0)
            {
                const auto Tuning=GetBreastTuning();const auto Dynamics=Tuning.DynamicsRest(R);
                Solver.Advance(*BreastProfile,Dynamics,World,Dt,FVector(0,0,GetWorld()->GetGravityZ()),Reset,Paused,Softness,Tuning.CouplingScale);
            }
            else if(!bJiggleEnabled) Solver.Reset();
            for(int32 I=0;I<R.Nodes.Num();++I)
            {
                const auto& N=R.Nodes[I];if(!Pose.IsValidIndex(N.BoneIndex)) continue;
                FVector Offset=Solver.Nodes.IsValidIndex(I) && bJiggleEnabled ? Solver.Nodes[I].Displacement : FVector::ZeroVector;
                // Small-angle orientation follows semantic lever deflection; no extra rotational DOF.
                FVector Rotation=FVector::CrossProduct(N.Rest,Offset)/FMath::Max(1.,N.Rest.SizeSquared());
                Rotation=Rotation.GetClampedToMaxSize(BreastProfile->MaximumRotationRadians);
                FTransform Local(Rotation.IsNearlyZero() ? FQuat::Identity : FQuat(Rotation.GetSafeNormal(),Rotation.Size()),N.Rest+Offset);
                Pose[N.BoneIndex]=Local*AnchorCS;
                if(bShowHelperBones || bShowDynamicNodes)
                {
                    const FVector P=World.TransformPosition(N.Rest+Offset);
                    DrawDebugLine(GetWorld(),World.GetLocation(),P,FColor::Cyan,false,0,0,.5);
                    DrawDebugPoint(GetWorld(),P,7,FColor::Yellow,false,0);
                    if(bShowDynamicNodes) DrawDebugLine(GetWorld(),World.TransformPosition(N.Rest),P,FColor::Red,false,0,0,2);
                }
            }
            if(bShowRegionWeights) for(int32 I=0;I<R.RegionPoints.Num();++I)
                if(R.RegionWeights.IsValidIndex(I) && R.RegionWeights[I]>.01)
                    DrawDebugPoint(GetWorld(),World.TransformPosition(R.RegionPoints[I]),3,FLinearColor(R.RegionWeights[I],0,1-R.RegionWeights[I]).ToFColor(false),false,0);
        }
        if(Dt>0 || LastTime<0) { LastTime=Now;LastTeleport=Teleport;bRebase=false;bWasEnabled=bJiggleEnabled; }
    }
    Super::FinalizeBoneTransform();
}
FString UVamBreastSkeletalMeshComponent::BreastDiagnostics() const
{
    if(!BreastProfile) return TEXT("Breast Jiggle: unsupported / profile absent; upgrade this character Runtime");
    FString Out=FString::Printf(TEXT("Breast Jiggle %s | density %.6f kg/cm3 | softness %.2f\n"),bJiggleEnabled?TEXT("Enabled"):TEXT("Disabled"),DensityOverrideKgPerCm3>0?DensityOverrideKgPerCm3:BreastProfile->DensityKgPerCm3,Softness);
    for(int32 I=0;I<RestSides.Num();++I)
    {
        const auto& R=RestSides[I];
        const auto Dynamics=GetBreastTuning().DynamicsRest(R);
        Out+=FString::Printf(TEXT("%s effective volume %.2f cm3 mass %.4f kg COM %s\n"),*R.Side.ToString(),R.EffectiveVolumeCm3,R.MassKg,*R.COM.ToCompactString());
        if(!Solvers.IsValidIndex(I)) continue;
        const auto& S=Solvers[I];
        Out+=FString::Printf(TEXT("v %s a %s omega %s alpha %s steps %d dropped %d sleep %d\n"),*S.LinearVelocity.ToCompactString(),*S.LinearAcceleration.ToCompactString(),*S.AngularVelocity.ToCompactString(),*S.AngularAcceleration.ToCompactString(),S.LastSteps,S.DroppedSteps,S.bSleeping);
        for(int32 N=0;N<S.Nodes.Num() && N<Dynamics.Nodes.Num();++N)
        {
            const auto& Node=Dynamics.Nodes[N];const auto& Offset=S.Nodes[N].Displacement;
            const FVector Hz=Node.FrequencyHz*FMath::Sqrt((R.ReferenceMassKg>0?R.ReferenceMassKg/R.MassKg:1.)/FMath::Clamp(FMath::IsFinite(Softness)?Softness:1.,.2,100.));
            double Usage=0;for(int32 Axis=0;Axis<3;++Axis) Usage=FMath::Max(Usage,FMath::Abs(Offset[Axis])/(Offset[Axis]>=0?Node.PositiveLimitCm[Axis]:Node.NegativeLimitCm[Axis]));
            Out+=FString::Printf(TEXT("%s: %s cm | small-signal Hz %s | damping %s | limit %.0f%%\n"),*Node.Semantic.ToString(),*Offset.ToCompactString(),*Hz.ToCompactString(),*Node.DampingRatio.ToCompactString(),Usage*100);
        }
        Out+=TEXT("\n");
    }
    return Out;
}

void UVamBreastSkeletalMeshComponent::BreastMotionCommand(FName Command)
{
    if(!GetOwner() || !GetWorld() || !GetWorld()->IsGameWorld()) return;
    DebugTime=0;
    if(Command==TEXT("Reset")) { DebugCommand=NAME_None;DebugVelocity=FVector::ZeroVector;ResetBreastJiggle();return; }
    if(Command==TEXT("Stop") || Command==TEXT("Stop rotation")) { DebugCommand=NAME_None;DebugVelocity=FVector::ZeroVector;return; }
    DebugCommand=Command;
    DebugDirection=Command==TEXT("Lateral accelerate") ? GetOwner()->GetActorRightVector() : GetOwner()->GetActorForwardVector();
    if(Command==TEXT("Jump impulse")) DebugVelocity=FVector(0,0,250);
}
void UVamBreastSkeletalMeshComponent::TickComponent(float Dt,ELevelTick TickType,FActorComponentTickFunction* TickFunction)
{
    const auto* Motion=GetOwner() ? GetOwner()->FindComponentByClass<UVamMotionComponent>() : nullptr;
    if(!DebugCommand.IsNone() && GetWorld() && GetWorld()->IsGameWorld() && !GetWorld()->IsPaused() && !(Motion && Motion->GetClock().bPaused))
    {
        FTransform Next=GetOwner()->GetActorTransform();
        if(DebugCommand==TEXT("Rotate continuously")) Next.SetRotation((FQuat(FVector::UpVector,Dt*1.5)*Next.GetRotation()).GetNormalized());
        else
        {
            FVector A=DebugDirection*150.;
            if(DebugCommand==TEXT("Jump impulse")) A=FVector(0,0,GetWorld()->GetGravityZ());
            if(DebugTime>1. && DebugCommand!=TEXT("Jump impulse")) A=FVector::ZeroVector;
            Next.AddToTranslation(DebugVelocity*Dt+A*(.5*Dt*Dt));DebugVelocity+=A*Dt;
            if(DebugCommand==TEXT("Jump impulse") && DebugTime+Dt>.5) { DebugCommand=NAME_None;DebugVelocity=FVector::ZeroVector; }
        }
        GetOwner()->SetActorTransform(Next,false,nullptr,ETeleportType::None);DebugTime+=Dt;
    }
    Super::TickComponent(Dt,TickType,TickFunction);
}

void UVamBreastSkeletalMeshComponent::SetBreastDensity(double Density)
{
    if(!FMath::IsFinite(Density) || Density<=0 || Density>.1) return;
    DensityOverrideKgPerCm3=Density;
    for(auto& R:RestSides) R.MassKg=R.EffectiveVolumeCm3*Density;
}

#include "VamBreastSkeletalMeshComponent.h"
#include "VamMotionComponent.h"
#include "Engine/World.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/Actor.h"
#include "DrawDebugHelpers.h"
#include "VamBreastCalibration.h"

FVamBreastTuning UVamBreastSkeletalMeshComponent::GetBreastTuning() const
{
    FVamBreastTuning Result;Result.Support=Support;Result.Damping=Damping;Result.Mobility=BreastMobility;
    Result.InternalCoupling=InternalCoupling;Result.MassScale=MassScale;Result.LegacyCompliance=Softness;return Result;
}
void UVamBreastSkeletalMeshComponent::ResetBreastTuning()
{
    BreastAmplitude=2;Support=Damping=BreastMobility=InternalCoupling=MassScale=1;ResetBreastJiggle();
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
            R.SizeCm+=Response.SizeDelta*Delta;R.RootSizeCm+=Response.RootSizeDelta*Delta;
            for(int32 N=0;N<R.Nodes.Num();++N)
            {
                if(Response.NodeDeltas.IsValidIndex(N)) R.Nodes[N].Rest+=Response.NodeDeltas[N]*Delta;
                if(Response.NodeMassCenterDeltas.IsValidIndex(N)) R.Nodes[N].MassCenter+=Response.NodeMassCenterDeltas[N]*Delta;
                if(Response.NodeVolumeLogSlopes.IsValidIndex(N)) R.Nodes[N].EffectiveVolumeCm3*=FMath::Exp(FMath::Clamp(Response.NodeVolumeLogSlopes[N]*Delta,-3.,3.));
            }
        }
        const double Ratio=FMath::Exp(FMath::Clamp(LogScale,-3.,3.));
        R.EffectiveVolumeCm3*=Ratio;R.MassKg=R.EffectiveVolumeCm3*(DensityOverrideKgPerCm3>0 ? DensityOverrideKgPerCm3 : BreastProfile->DensityKgPerCm3);
        R.EffectiveRadiusCm=FMath::Max(.1,R.EffectiveRadiusCm);
        R.EffectiveDepthCm=FMath::Max(.01,R.EffectiveDepthCm);R.SupportAreaCm2=FMath::Max(.01,R.SupportAreaCm2);
        if(BreastProfile->SchemaVersion>=2)
        {
            for(int32 Axis=0;Axis<3;++Axis) { R.SizeCm[Axis]=FMath::Max(.1,R.SizeCm[Axis]);R.RootSizeCm[Axis]=FMath::Max(.1,R.RootSizeCm[Axis]); }
            VamBreastCalibration::Calibrate(R,BreastProfile->DensityKgPerCm3,BreastProfile->EffectiveModulusPa);
        }
        if(Reference.IsValidIndex(R.AnchorBone)) Reference[R.AnchorBone]=R.AnchorLocal;
        for(const auto& N:R.Nodes) if(Reference.IsValidIndex(N.BoneIndex)) Reference[N.BoneIndex].SetTranslation(N.Rest);
        if(Previous.IsValidIndex(Side) && Solvers.IsValidIndex(Side) &&
            FMath::Abs(R.EffectiveVolumeCm3/Previous[Side].EffectiveVolumeCm3-1)>BreastProfile->LargeShapeChangeRatio) Solvers[Side].Reset();
    }
    // Shape transactions rebase motion history, never differentiate the user's morph edits.
    for(int32 I=0;I<Solvers.Num();++I) { Solvers[I].Samples=0;Solvers[I].Accumulator=0;if(RestSides.IsValidIndex(I)) Solvers[I].ProjectResidual(RestSides[I]); }
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
                const auto Tuning=GetBreastTuning();
                Solver.Advance(*BreastProfile,R,World,Dt,FVector(0,0,GetWorld()->GetGravityZ()),Reset,Paused,Tuning);
            }
            else if(!bJiggleEnabled) Solver.Reset();
            const double Amplitude=FMath::Clamp(FMath::IsFinite(BreastAmplitude)?BreastAmplitude:2.,0.,10.);
            for(int32 I=0;I<R.Nodes.Num();++I)
            {
                const auto& N=R.Nodes[I];if(!Pose.IsValidIndex(N.BoneIndex)) continue;
                FVector Offset=FVector::ZeroVector;
                if(bJiggleEnabled) Offset=BreastProfile->SchemaVersion>=2?Solver.NodeOffset(R,I):(Solver.Nodes.IsValidIndex(I)?Solver.Nodes[I].Displacement:FVector::ZeroVector);
                const FVector Angular=bJiggleEnabled && BreastProfile->SchemaVersion>=2?Solver.AngularDisplacement:FVector::ZeroVector;
                // Legacy orientation remains available only for schema 1; v2 uses integrated angular state.
                FVector Rotation=BreastProfile->SchemaVersion>=2?Angular:FVector::CrossProduct(N.Rest,Offset)/FMath::Max(1.,N.Rest.SizeSquared());
                if(BreastProfile->SchemaVersion==1) Rotation=Rotation.GetClampedToMaxSize(BreastProfile->MaximumRotationRadians);
                Offset*=Amplitude;Rotation*=Amplitude;
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
    FString Out=FString::Printf(TEXT("Breast amplitude %.2f (1 = original output)\n"),BreastAmplitude);
    Out+=FString::Printf(TEXT("Breast calibration schema %d | %s\n"),BreastProfile->SchemaVersion,*BreastProfile->BuildAlgorithmVersion);
    if(BreastProfile->SchemaVersion==1) Out+=TEXT("Legacy profile: use Upgrade Runtime for calibrated controls and angular dynamics.\n");
    for(int32 I=0;I<RestSides.Num();++I)
    {
        const auto& R=RestSides[I];const auto E=GetBreastTuning().DynamicsRest(R);
        Out+=FString::Printf(TEXT("%s Volume %.2f cm3 Mass %.4f kg COM %s Root %.2f cm2 Depth %.2f cm Size AP/ML/SI %s\nInertia diag %s offdiag(XY/XZ/YZ) %s kg cm2\n"),*R.Side.ToString(),R.EffectiveVolumeCm3,E.MassKg,*R.COM.ToCompactString(),R.SupportAreaCm2,R.EffectiveDepthCm,*R.SizeCm.ToCompactString(),*E.InertiaDiagonal.ToCompactString(),*E.InertiaOffDiagonal.ToCompactString());
        if(!Solvers.IsValidIndex(I)) continue;
        const auto& S=Solvers[I];
        Out+=FString::Printf(TEXT("v %s a %s omega %s alpha %s steps %d dropped %d sleep %d emergency limits %d\nAngular displacement %s rad velocity %s rad/s\n"),*S.LinearVelocity.ToCompactString(),*S.LinearAcceleration.ToCompactString(),*S.AngularVelocity.ToCompactString(),*S.AngularAcceleration.ToCompactString(),S.LastSteps,S.DroppedSteps,S.bSleeping,S.LimitCorrections,*S.AngularDisplacement.ToCompactString(),*S.RelativeAngularVelocity.ToCompactString());
        Out+=FString::Printf(TEXT("COM displacement %s cm velocity %s cm/s\nCentrifugal load %s kg cm/s2 | modal force translation %.4f residual %.4f torque %.4f kg cm2/s2\n"),*S.COMDisplacement.ToCompactString(),*S.COMVelocity.ToCompactString(),*S.CentrifugalLoad.ToCompactString(),S.TranslationLoadMagnitude,S.ResidualLoadMagnitude,S.RotationLoadMagnitude);
        for(int32 N=0;N<E.Nodes.Num();++N)
        {
            const auto& Node=E.Nodes[N];
            FVector Hz;for(int32 A=0;A<3;++A) Hz[A]=FMath::Sqrt(Node.SupportStiffness[A]/FMath::Max(1.e-8,E.MassKg*Node.MassFraction))/(2*PI);
            Out+=FString::Printf(TEXT("%s mass %.1f%% volume %.2f lever %.2f | support %s kg/s2 Hz %s damping %s | travel +%s -%s cm\n"),*Node.Semantic.ToString(),Node.MassFraction*100,Node.EffectiveVolumeCm3,Node.LeverArmCm,*Node.SupportStiffness.ToCompactString(),*Hz.ToCompactString(),*Node.DampingRatio.ToCompactString(),*Node.PositiveLimitCm.ToCompactString(),*Node.NegativeLimitCm.ToCompactString());
            if(S.Nodes.IsValidIndex(N)) Out+=TEXT("residual displacement ")+S.Nodes[N].Displacement.ToCompactString()+TEXT(" cm\n");
        }
    }
    return Out;
}

void UVamBreastSkeletalMeshComponent::BreastMotionCommand(FName Command)
{
    if(!GetOwner() || !GetWorld() || !GetWorld()->IsGameWorld()) return;
    if(Command==TEXT("Reset")) { bDebugLinear=bDebugYaw=bDebugJump=false;ResetBreastJiggle();return; }
    if(Command==TEXT("Jump") || Command==TEXT("Jump impulse"))
    {
        if(!bDebugJump) { DebugJump.Gravity=FMath::Max(1.,-GetWorld()->GetGravityZ());DebugJumpTime=0;bDebugJump=true; }return;
    }
    const bool Yaw=Command==TEXT("Smooth Rotate Start") || Command==TEXT("Continuous Rotate") || Command==TEXT("Rotate continuously") || Command==TEXT("Smooth Rotate Stop") || Command==TEXT("Stop rotation") || Command==TEXT("Hard Rotate Stop");
    if(Yaw)
    {
        const bool Stop=Command==TEXT("Smooth Rotate Stop") || Command==TEXT("Stop rotation") || Command==TEXT("Hard Rotate Stop");
        double X=0,V=0,A=0,J=0;if(bDebugYaw) DebugYaw.Evaluate(DebugYawTime,X,V,A,J);
        if(Command==TEXT("Hard Rotate Stop")) { V=A=J=0; }
        DebugYaw.Start(V,A,J,Stop?0:1.5,Stop?.4:.6);DebugYawTime=0;bDebugYaw=true;return;
    }
    const bool Stop=Command==TEXT("Smooth Stop") || Command==TEXT("Stop") || Command==TEXT("Hard Stop");
    const FVector Direction=Command==TEXT("Lateral accelerate")?GetOwner()->GetActorRightVector():GetOwner()->GetActorForwardVector();
    for(int32 I=0;I<3;++I)
    {
        double X=0,V=0,A=0,J=0;if(bDebugLinear) DebugLinear[I].Evaluate(DebugLinearTime,X,V,A,J);
        if(Command==TEXT("Hard Stop")) V=A=J=0;
        DebugLinear[I].Start(V,A,J,Stop?0:150*Direction[I],Stop?.4:1.);
    }
    DebugLinearTime=0;bDebugLinear=true;
}
void UVamBreastSkeletalMeshComponent::TickComponent(float Dt,ELevelTick TickType,FActorComponentTickFunction* TickFunction)
{
    const auto* Motion=GetOwner()?GetOwner()->FindComponentByClass<UVamMotionComponent>():nullptr;
    if((bDebugLinear || bDebugYaw || bDebugJump) && GetWorld() && GetWorld()->IsGameWorld() && !GetWorld()->IsPaused() && !(Motion && Motion->GetClock().bPaused))
    {
        FTransform Next=GetOwner()->GetActorTransform();double Old=0,New=0,V=0,A=0,J=0;
        if(bDebugLinear)
        {
            FVector Delta=FVector::ZeroVector;
            for(int32 I=0;I<3;++I) { DebugLinear[I].Evaluate(DebugLinearTime,Old,V,A,J);DebugLinear[I].Evaluate(DebugLinearTime+Dt,New,V,A,J);Delta[I]=New-Old; }
            Next.AddToTranslation(Delta);DebugLinearTime+=Dt;
        }
        if(bDebugYaw)
        {
            DebugYaw.Evaluate(DebugYawTime,Old,V,A,J);DebugYaw.Evaluate(DebugYawTime+Dt,New,V,A,J);
            Next.SetRotation((FQuat(FVector::UpVector,New-Old)*Next.GetRotation()).GetNormalized());DebugYawTime+=Dt;
        }
        if(bDebugJump)
        {
            DebugJump.Evaluate(DebugJumpTime,Old,V,A,J);DebugJump.Evaluate(DebugJumpTime+Dt,New,V,A,J);
            Next.AddToTranslation(FVector(0,0,New-Old));DebugJumpTime+=Dt;if(DebugJumpTime>=DebugJump.Duration()) bDebugJump=false;
        }
        GetOwner()->SetActorTransform(Next,false,nullptr,ETeleportType::None);
    }
    Super::TickComponent(Dt,TickType,TickFunction);
}

void UVamBreastSkeletalMeshComponent::SetBreastDensity(double Density)
{
    if(BreastProfile && BreastProfile->SchemaVersion>=2) return;
    if(!FMath::IsFinite(Density) || Density<=0 || Density>.1) return;
    DensityOverrideKgPerCm3=Density;
    for(auto& R:RestSides) R.MassKg=R.EffectiveVolumeCm3*Density;
}

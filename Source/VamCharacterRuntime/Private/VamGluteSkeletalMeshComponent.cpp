#include "VamGluteSkeletalMeshComponent.h"
#include "VamCharacterComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "DrawDebugHelpers.h"

void UVamGluteSkeletalMeshComponent::UpdateGluteShape(const TMap<FName,float>& Values,TArray<FTransform>& Reference,int32 ShapeRevision)
{
    if(!GluteProfile || !GluteProfile->IsValidProfile() || !GetSkeletalMeshAsset()) return;
    const auto OldRest=GluteRest;bGluteShapeRebase=true;
    GluteShapeRevision=ShapeRevision;GluteRest=GluteProfile->Sides;
    auto CS=Reference;const auto& Ref=GetSkeletalMeshAsset()->GetRefSkeleton();
    for(int32 I=0;I<CS.Num();++I) if(Ref.GetParentIndex(I)>=0) CS[I]=CS[I]*CS[Ref.GetParentIndex(I)];
    for(auto& S:GluteRest)
    {
        const FTransform Anchor=S.AnchorLocal*CS[S.PelvisBone];
        const FVector FemurLocal=S.RestThighInAnchor.GetRotation().UnrotateVector(S.FemurAxisInAnchor);
        S.RestThighInAnchor=CS[S.ThighBone].GetRelativeTransform(Anchor);
        S.FemurAxisInAnchor=S.RestThighInAnchor.GetRotation().RotateVector(FemurLocal);
        VamGluteStructure::ApplyShape(S,Values);
        for(auto& R:S.Regions)
        {
            R.SupportBaseline=GluteProfile->EffectiveModulusPa*.01*S.SupportAreaCm2*R.MassFractionCandidate/FMath::Max(.1,R.LeverArmCm);
            const FVector D=R.MassCenter-S.COM,Size=S.Dimensions*.5;const double Mass=R.EffectiveVolumeCm3*GluteProfile->DensityCandidateKgPerCm3;
            R.InertiaCandidate=Mass*FVector(D.Y*D.Y+D.Z*D.Z+(Size.Y*Size.Y+Size.Z*Size.Z)/12,D.X*D.X+D.Z*D.Z+(Size.X*Size.X+Size.Z*Size.Z)/12,D.X*D.X+D.Y*D.Y+(Size.X*Size.X+Size.Y*Size.Y)/12);
        }
        if(GluteProfile->RefinementVersion==1) VamGluteStructure::CalibratePoseRefinement(S);
        Reference[S.AnchorBone]=S.AnchorLocal;
        for(const auto& R:S.Regions) Reference[R.BoneIndex]=FTransform(VamGluteStructure::FiberBasis(S,R),R.Rest);
    }
    if(GluteJiggleProfile && OldRest.Num()==2) for(int32 I=0;I<2;++I) if(FMath::Abs(GluteRest[I].EffectiveVolumeCm3/OldRest[I].EffectiveVolumeCm3-1)>GluteJiggleProfile->LargeShapeChangeRatio) GluteSolvers[I].Reset();
}
void UVamGluteSkeletalMeshComponent::FinalizeBoneTransform()
{
    if(GluteProfile && GluteProfile->IsValidProfile())
    {
        if(GluteRest.IsEmpty()) GluteRest=GluteProfile->Sides;
        auto& Pose=GetEditableComponentSpaceTransforms();GluteStates.Reset();
        // Snapshot all primary inputs before writing either side's helpers.
        if(GluteRest.Num()==2 && Pose.IsValidIndex(GluteRest[0].PelvisBone) && Pose.IsValidIndex(GluteRest[0].ThighBone) && Pose.IsValidIndex(GluteRest[1].ThighBone))
            HipPoseState=VamGluteStructure::CaptureHipPose(*GluteProfile,GluteRest,Pose[GluteRest[0].PelvisBone],Pose[GluteRest[0].ThighBone],Pose[GluteRest[1].ThighBone],GluteShapeRevision);
        for(int32 SideIndex=0;SideIndex<GluteRest.Num();++SideIndex)
        {
            const auto& S=GluteRest[SideIndex];
            if(!Pose.IsValidIndex(S.AnchorBone) || !Pose.IsValidIndex(S.ThighBone)) continue;
            const FTransform Anchor=S.AnchorLocal*Pose[S.PelvisBone];
            const FTransform Thigh=HipPoseState.Sides.IsValidIndex(SideIndex)?HipPoseState.Sides[SideIndex].FemurInAnchor:Pose[S.ThighBone].GetRelativeTransform(Anchor);
            auto State=VamGluteStructure::Evaluate(*GluteProfile,S,bGluteEnabled?Thigh:S.RestThighInAnchor);
            Pose[S.AnchorBone]=Anchor;
            const FTransform World=Anchor*GetComponentTransform();
            for(int32 I=0;I<S.Regions.Num();++I)
            {
                const auto& R=S.Regions[I];const auto& N=State.Regions[I];
                if(Pose.IsValidIndex(R.BoneIndex)) Pose[R.BoneIndex]=N.Transform*Anchor;
                if(!GetWorld()) continue;
                const FVector P=World.TransformPosition(N.Transform.GetLocation());
                if(bShowStructuralBones) { DrawDebugLine(GetWorld(),World.GetLocation(),P,FColor::Cyan,false,0);DrawDebugPoint(GetWorld(),P,7,FColor::Yellow,false,0); }
                if(bShowPelvisAttachments) DrawDebugLine(GetWorld(),World.TransformPosition(R.PelvisPoint),P,FColor::Green,false,0,0,1);
                if(bShowThighAttachments) DrawDebugLine(GetWorld(),World.TransformPosition(N.ThighPoint),P,FColor::Orange,false,0,0,1);
                if(bShowPoseTension) DrawDebugString(GetWorld(),P,FString::Printf(TEXT("%s %.3f"),*R.Semantic.ToString(),N.Tension),nullptr,FColor::White,0);
            }
            if(bShowGluteRegion && GetWorld()) for(int32 I=0;I<S.RegionPoints.Num();++I)
                if(S.RegionWeights[I]>.02) DrawDebugPoint(GetWorld(),World.TransformPosition(S.RegionPoints[I]),3,FLinearColor(S.RegionWeights[I],.1f,1-S.RegionWeights[I]).ToFColor(false),false,0);
            if(bShowFoldSemantics && GetWorld())
            {
                const auto& M=S.FoldSemanticMap;
                for(const FVector& Point:{M.MedialInfraglutealAnchor,M.MiddleTransition,M.LateralFade}) DrawDebugPoint(GetWorld(),World.TransformPosition(Point),9,FColor::Magenta,false,0);
            }
            GluteStates.Add(MoveTemp(State));
        }
    }
    ApplyGluteCorrectives();
    ApplyGluteJiggle();
    // Both use final rigid-blended source poses; writes are disjoint. Breast then publishes native buffers.
    Super::FinalizeBoneTransform();
}
FString UVamGluteSkeletalMeshComponent::GluteDiagnostics() const
{
    if(!GluteProfile) return TEXT("G0 profile absent / unsupported. Select saved character and Upgrade Runtime.");
    FString Out=GluteProfile->Algorithm+TEXT(" | deterministic pose function; no Jiggle\n");
    Out+=FString::Printf(TEXT("Shape revision %d | pelvis tilt/yaw/roll deg %.2f / %.2f / %.2f\n"),HipPoseState.ShapeRevision,FMath::RadiansToDegrees(HipPoseState.PelvisTilt),FMath::RadiansToDegrees(HipPoseState.PelvisYaw),FMath::RadiansToDegrees(HipPoseState.PelvisRoll));
    for(int32 I=0;I<GluteRest.Num();++I)
    {
        const auto& S=GluteRest[I];
        Out+=FString::Printf(TEXT("%s effective volume %.2f cm3 COM %s AP/ML/SI %s cm\n"),*S.Side.ToString(),S.EffectiveVolumeCm3,*S.COM.ToCompactString(),*S.Dimensions.ToCompactString());
        if(!GluteStates.IsValidIndex(I)) continue;
        const auto& State=GluteStates[I];
        FVector InputAngles=State.HipAnglesDegrees;
        if(HipPoseState.Sides.IsValidIndex(I)) { const auto& H=HipPoseState.Sides[I];InputAngles=FVector(H.FlexionExtension,H.AbductionAdduction,H.ExternalInternalRotation)*(180./PI); }
        Out+=TEXT("primary hip flex/abd/external deg ")+InputAngles.ToCompactString()+TEXT("\n");
        Out+=FString::Printf(TEXT("Fold medial/middle/lateral %.3f / %.3f / %.3f | stretch %s | final COM %s\n"),State.FoldState.MedialAnchorFactor,State.FoldState.MiddleTransitionFactor,State.FoldState.LateralFadeFactor,*State.FoldState.StretchState.ToCompactString(),*State.FinalRestCOM.ToCompactString());
        for(int32 J=0;J<S.Regions.Num();++J)
        {
            const auto& R=S.Regions[J];const auto& N=State.Regions[J];
            Out+=TEXT("orientation ")+N.OrientationAdjustment.Rotator().ToCompactString()+TEXT(" | AP/ML/SI support ")+N.RegionalStiffnessBaseline.ToCompactString()+TEXT("\n");
            Out+=FString::Printf(TEXT("%s pelvis/thigh %.3f/%.3f -> %.3f/%.3f | offset %s | passive %.3f | support %.3f\n"),*R.Semantic.ToString(),R.PelvisAttachment,R.ThighAttachment,N.PelvisAttachment,N.ThighAttachment,*(N.Transform.GetLocation()-R.Rest).ToCompactString(),N.Tension,N.Support);
        }
    }
    if(CorrectiveProfile)
    {
        Out+=TEXT("G06 ")+CorrectiveProfile->Provenance+TEXT("\n");
        Out+=CorrectiveProfile->Algorithm+TEXT(" | ")+CorrectiveProfile->GetPathName()+TEXT("\n");
        for(int32 Side=0;Side<CorrectiveTargetWeights.Num();++Side)
            for(int32 T=0;T<CorrectiveTargetWeights[Side].Num();++T) if(CorrectiveTargetWeights[Side][T]>.001)
                Out+=FString::Printf(TEXT("%s %s weight %.4f\n"),Side==0?TEXT("L"):TEXT("R"),*CorrectiveProfile->Targets[T].Name.ToString(),CorrectiveTargetWeights[Side][T]);
        Out+=TEXT("Corrective Diagnostics: selected target at build Shape, true surface samples (cm); not current blended/Shape preview measurement.\n");
        for(const auto& D:CorrectiveProfile->Diagnostics) if(D.Target==CorrectiveDiagnosticTarget && (DebugGluteSide<0 || D.Side==DebugGluteSide))
        {
            Out+=FString::Printf(TEXT("%s %s | source/procedural RMS %.5f / %.5f | raw/adapted/final P95 %.5f / %.5f / %.5f\n"),D.Side==0?TEXT("L"):TEXT("R"),*CorrectiveProfile->Targets[D.Target].Name.ToString(),D.SourceRms,D.ProceduralRms,D.RawP95,D.AdaptedP95,D.FinalP95);
            Out+=FString::Printf(TEXT("source retention %.4f | target safety loss %.4f | smoothing RMS loss %.6f | affected surface vertices %d\n"),D.AttenuationRatio,D.SafetyLoss,D.SmoothingLoss,D.AffectedVertices);
            if(CorrectiveProfile->SchemaVersion>=3) Out+=FString::Printf(TEXT("Independent skinning residual RMS %.5f cm\n"),D.SkinningResidualRms);
        }
    }
    else Out+=TEXT("G06 profile absent: Upgrade Runtime to create corrective geometry.\n");
    return Out;
}
void UVamGluteSkeletalMeshComponent::GlutePoseCommand(FName Command)
{
    auto* C=GetOwner()?GetOwner()->FindComponentByClass<UVamCharacterComponent>():nullptr;
    if(!C || !GluteProfile) return;
    for(int32 I=0;I<GluteProfile->Sides.Num();++I)
    {
        if(DebugGluteSide>=0 && DebugGluteSide!=I && Command!=TEXT("Reset")) continue;
        const auto& S=GluteProfile->Sides[I];
        FVector Axis=FVector::YAxisVector;double Angle=0;
        if(Command==TEXT("Hip flexion")) Angle=70;
        if(Command==TEXT("Flexion 30")) Angle=30;
        if(Command==TEXT("Flexion 60")) Angle=60;
        if(Command==TEXT("Flexion 90")) Angle=90;
        if(Command==TEXT("Hip extension")) Angle=-20;
        if(Command==TEXT("Abduction")) { Axis=FVector::XAxisVector;Angle=S.SideSign*30; }
        if(Command==TEXT("Adduction")) { Axis=FVector::XAxisVector;Angle=-S.SideSign*20; }
        if(Command==TEXT("External rotation") || Command==TEXT("Internal rotation")) { Axis=S.FemurAxisInAnchor;Angle=S.SideSign*(Command==TEXT("External rotation")?30:-30); }
        C->SetDebugBoneOffset(S.ThighBone,FTransform(FQuat(S.AnchorLocal.TransformVectorNoScale(Axis),FMath::DegreesToRadians(Angle))));
    }
}

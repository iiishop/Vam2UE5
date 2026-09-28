#include <cmath>
#include "VamGluteStructure.h"

bool UVamGluteStructureProfile::IsValidProfile() const
{
    if(SchemaVersion!=1 || Sides.Num()!=2 || SourceTopologyIdentity.IsEmpty() || !FMath::IsFinite(MaximumLogStretch) || MaximumLogStretch<=0 || !FMath::IsFinite(PassiveTensionGain) || PassiveTensionGain<0) return false;
    for(const auto& S:Sides)
    {
        if(S.Regions.Num()!=5 || S.PelvisBone<0 || S.ThighBone<0 || S.AnchorBone<SourceBoneCount || !FMath::IsFinite(S.EffectiveVolumeCm3) || S.EffectiveVolumeCm3<=0) return false;
        for(const auto& A:S.ShapeResponses) if(A.RestDeltas.Num()!=5 || A.PelvisDeltas.Num()!=5 || A.ThighDeltas.Num()!=5 || A.VolumeSlopes.Num()!=5 || A.MassCenterDeltas.Num()!=5 || A.PelvisAttachmentDeltas.Num()!=5) return false;
        for(const auto& R:S.Regions) if(R.BoneIndex<=S.AnchorBone || R.Rest.ContainsNaN() || R.PelvisPoint.ContainsNaN() || R.ThighPointLocal.ContainsNaN() || !FMath::IsFinite(R.SupportBaseline) || R.SupportBaseline<=0 || !FMath::IsFinite(R.ThighAttachment) || !FMath::IsFinite(R.PelvisAttachment) || R.PelvisAttachment<=0 || R.ThighAttachment<=0 || FMath::Abs(R.PelvisAttachment+R.ThighAttachment-1)>1.e-6) return false;
    }
    return true;
}
FQuat VamGluteStructure::FiberBasis(const FVamGluteSide& S,const FVamGluteRegion& R)
{
    const FVector Fiber=S.RestThighInAnchor.TransformPosition(R.ThighPointLocal)-R.PelvisPoint;
    return FRotationMatrix::MakeFromXZ(Fiber.GetSafeNormal(),FVector::UpVector).ToQuat();
}
FVamGluteStructuralState VamGluteStructure::Evaluate(const UVamGluteStructureProfile& P,const FVamGluteSide& S,const FTransform& ThighInAnchor)
{
    FVamGluteStructuralState Out;
    FQuat Delta=(ThighInAnchor.GetRotation()*S.RestThighInAnchor.GetRotation().Inverse()).GetNormalized();
    if(Delta.W<0) Delta=Delta*-1.;
    FVector Axis;double Angle;Delta.ToAxisAndAngle(Axis,Angle);
    const FVector Radians=Axis*Angle;
    Out.HipAnglesDegrees=FVector(Radians.Y,S.SideSign*Radians.X,-S.SideSign*Radians.Z)*(180./PI);
    for(const auto& R:S.Regions)
    {
        FVamGluteRegionState N;
        const FVector RestFiber=S.RestThighInAnchor.TransformPosition(R.ThighPointLocal)-R.PelvisPoint;
        N.ThighPoint=ThighInAnchor.TransformPosition(R.ThighPointLocal);
        const FVector Fiber=N.ThighPoint-R.PelvisPoint;
        const double LogRatio=FMath::Loge(FMath::Max(.001,Fiber.Size())/FMath::Max(.001,RestFiber.Size()));
        const double Strain=FMath::Max(0.,LogRatio);
        N.Tension=P.PassiveTensionGain*Strain*Strain;
        // Passive fiber support grows smoothly; the pelvis tether is never released.
        const double TP=R.ThighAttachment*(1+N.Tension);
        const double PP=R.PelvisAttachment*(1+N.Tension*R.PelvisAttachment);
        N.PelvisAttachment=PP/(PP+TP);N.ThighAttachment=TP/(PP+TP);
        N.Support=R.SupportBaseline*(PP+TP);
        const FQuat Bend=FQuat::Slerp(FQuat::Identity,FQuat::FindBetweenNormals(RestFiber.GetSafeNormal(),Fiber.GetSafeNormal()),N.ThighAttachment).GetNormalized();
        const double LogStretch=P.MaximumLogStretch*std::tanh(LogRatio*N.ThighAttachment/P.MaximumLogStretch);
        const double Axial=FMath::Exp(LogStretch),Radial=FMath::Exp(-.5*LogStretch);
        const FQuat Basis=FiberBasis(S,R);
        const FVector Scale(Axial,Radial,Radial); // determinant exactly one: regional scaffold volume responsibility
        FVector Position=R.PelvisPoint+Bend.RotateVector(Basis.RotateVector(Basis.UnrotateVector(R.Rest-R.PelvisPoint)*Scale));
        // Smooth posterior support barrier in the pelvis frame; exact imported neutral position.
        const double Floor=R.Rest.X*R.PelvisAttachment,Gap=FMath::Max(.001,R.Rest.X-Floor);
        Position.X=Floor+Gap*FMath::Exp(std::tanh((Position.X-R.Rest.X)/Gap));
        N.Transform=FTransform((Bend*Basis).GetNormalized(),Position,Scale);
        Out.Regions.Add(N);
    }
    return Out;
}
void VamGluteStructure::ApplyShape(FVamGluteSide& S,const TMap<FName,float>& Values)
{
    double VolumeLogScale=0;
    for(const auto& A:S.ShapeResponses)
    {
        const float* V=Values.Find(A.Parameter);const double D=(V?*V:A.DefaultValue)-A.DefaultValue;
        VolumeLogScale+=A.LogVolume*D;S.COM+=A.COM*D;S.Dimensions+=A.Dimensions*D;S.SupportAreaCm2+=A.SupportAreaDelta*D;
        for(int32 I=0;I<S.Regions.Num();++I)
        {
            auto& R=S.Regions[I];R.Rest+=A.RestDeltas[I]*D;R.PelvisPoint+=A.PelvisDeltas[I]*D;R.ThighPointLocal+=A.ThighDeltas[I]*D;
            R.PelvisAttachment+=A.PelvisAttachmentDeltas[I]*D;R.MassCenter+=A.MassCenterDeltas[I]*D;
            R.EffectiveVolumeCm3*=FMath::Exp(FMath::Clamp(A.VolumeSlopes[I]*D,-3.,3.));
        }
    }
    S.EffectiveVolumeCm3*=FMath::Exp(FMath::Clamp(VolumeLogScale,-3.,3.));
    for(int32 A=0;A<3;++A) S.Dimensions[A]=FMath::Max(.1,S.Dimensions[A]);
    double Total=0;for(const auto& R:S.Regions) Total+=R.EffectiveVolumeCm3;
    S.SupportAreaCm2=FMath::Max(.01,S.SupportAreaCm2);
    for(auto& R:S.Regions)
    {
        R.Rest.X=FMath::Max(.01,R.Rest.X);R.MassFractionCandidate=R.EffectiveVolumeCm3/FMath::Max(1.e-9,Total);R.EffectiveVolumeCm3=S.EffectiveVolumeCm3*R.MassFractionCandidate;
        R.PelvisAttachment=FMath::Clamp(R.PelvisAttachment,.02,.98);R.ThighAttachment=1-R.PelvisAttachment;
        R.LeverArmCm=(S.RestThighInAnchor.TransformPosition(R.ThighPointLocal)-R.PelvisPoint).Size();
    }
}

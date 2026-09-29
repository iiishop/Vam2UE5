#include "VamLegJiggleProfile.h"

bool UVamLegJiggleProfile::IsValidProfile() const
{
    if(SchemaVersion!=1 || Segments.Num()!=4 || !Integration || Integration->SchemaVersion!=2 || Integration->FixedStep<=0 || SourceBoneCount<1 || SourceBoneNames.Num()!=SourceBoneCount || SourceLocalBind.Num()!=SourceBoneCount || SourceTopologyIdentity.IsEmpty()) return false;
    TSet<int32> Helpers;
    for(const auto& S:Segments)
    {
        if(S.Dynamics.Nodes.Num()!=5 || S.Dynamics.Couplings.Num()!=8 || S.JointRest.Num()!=3 || S.ParentRestRotations.Num()!=3 || S.LengthCoefficients.Num()!=5 || S.SideCoefficients.Num()!=5 || S.RegionPoints.Num()!=S.RegionWeights.Num() || S.Length<=0 || S.Radius<=0 || !FMath::IsFinite(S.EffectiveVolumeCm3) || S.EffectiveVolumeCm3<=0 || S.Dynamics.ReferenceGravityLocal.ContainsNaN()) return false;
        for(int32 B:{S.Pelvis,S.Thigh,S.Shin,S.Foot}) if(B<0 || B>=SourceBoneCount) return false;
        if(S.AnchorBone<SourceBoneCount || Helpers.Contains(S.AnchorBone)) return false;Helpers.Add(S.AnchorBone);
        double Mass=0;
        for(const auto& N:S.Dynamics.Nodes) {if(N.BoneIndex<SourceBoneCount || Helpers.Contains(N.BoneIndex) || N.Rest.ContainsNaN() || N.Support.ContainsNaN() || N.MassKg<=0 || N.Support.GetMin()<=0 || N.PositiveTravel.GetMin()<=0) return false;Helpers.Add(N.BoneIndex);Mass+=N.MassKg;}
        if(!FMath::IsNearlyEqual(Mass,S.Dynamics.MassKg,1.e-6)) return false;
    }
    return Helpers.Num()==24;
}
FVector VamLegDynamics::JointAngles(const FVamLegSegment& S,const TArray<FTransform>& Pose,FVector2D& SideAngles)
{
    const int32 Child[]={S.Thigh,S.Shin,S.Foot},Parent[]={S.Pelvis,S.Thigh,S.Shin};FVector Angles=FVector::ZeroVector;SideAngles=FVector2D::ZeroVector;
    for(int32 I=0;I<3;++I)
    {
        FQuat Q=Pose[Child[I]].GetRelativeTransform(Pose[Parent[I]]).GetRotation()*S.JointRest[I].GetRotation().Inverse();Q.Normalize();if(Q.W<0) Q=Q*-1.;
        FVector Axis;double Angle;Q.ToAxisAndAngle(Axis,Angle);const FVector Rotation=S.ParentRestRotations[I].RotateVector(Axis.GetSafeNormal()*Angle);
        Angles[I]=FVector::DotProduct(Rotation,S.LateralAxis)*(I==1?1.:-1.);
        if(I==0) {const FVector LongAxis=S.BodyUp;SideAngles.X=FVector::DotProduct(Rotation,FVector::CrossProduct(S.LateralAxis,LongAxis).GetSafeNormal());SideAngles.Y=FVector::DotProduct(Rotation,LongAxis);}
    }
    return Angles;
}
FVamGluteDynamicSide VamLegDynamics::Evaluate(const UVamLegJiggleProfile& P,const FVamLegSegment& S,const FVector& Angles,const FVector2D& SideAngles,TArray<double>& Tension)
{
    auto R=S.Dynamics;Tension.Reset();
    for(int32 I=0;I<5;++I)
    {
        const double LogLength=FVector::DotProduct(S.LengthCoefficients[I],Angles)+S.SideCoefficients[I].X*FMath::Abs(SideAngles.X)+S.SideCoefficients[I].Y*FMath::Abs(SideAngles.Y);
        const double Stretch=FMath::Max(0.,FMath::Exp(FMath::Clamp(LogLength,-.5,.5))-1-P.PassiveSlack);
        const double Passive=1-FMath::Exp(-12*Stretch*Stretch);Tension.Add(Passive);
        R.Nodes[I].Support*=FVector(1+P.PassiveGain*Passive,1+P.PassiveGain*Passive,1+P.PassiveGain*Passive*1.5);
        R.Nodes[I].DampingRatio+=FVector(P.PassiveDampingGain*Passive);
    }
    return R;
}

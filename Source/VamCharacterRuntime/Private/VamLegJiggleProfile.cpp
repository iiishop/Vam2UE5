#include "VamLegJiggleProfile.h"

bool UVamLegJiggleProfile::IsValidProfile() const
{
    if((SchemaVersion!=1 && SchemaVersion!=2 && SchemaVersion!=3) || Segments.Num()!=4 || !Integration || Integration->SchemaVersion!=2 || Integration->FixedStep<=0 || SourceBoneCount<1 || SourceBoneNames.Num()!=SourceBoneCount || SourceLocalBind.Num()!=SourceBoneCount || SourceTopologyIdentity.IsEmpty()) return false;
    TSet<int32> Helpers;
    int SegmentIndex=0;
    for(const auto& S:Segments)
    {
        if(S.Dynamics.Nodes.Num()!=5 || S.Dynamics.Couplings.Num()!=8 || S.JointRest.Num()!=3 || S.ParentRestRotations.Num()!=3 || S.LengthCoefficients.Num()!=5 || S.SideCoefficients.Num()!=5 || S.RegionPoints.Num()!=S.RegionWeights.Num() || S.Length<=0 || S.Radius<=0 || !FMath::IsFinite(S.EffectiveVolumeCm3) || S.EffectiveVolumeCm3<=0 || S.Dynamics.ReferenceGravityLocal.ContainsNaN()) return false;
        for(int32 B:{S.Pelvis,S.Thigh,S.Shin,S.Foot}) if(B<0 || B>=SourceBoneCount) return false;
        if(S.AnchorBone<SourceBoneCount || Helpers.Contains(S.AnchorBone)) return false;Helpers.Add(S.AnchorBone);
        if(SchemaVersion==3) {
            if(ChainBoneIndices.Num()!=20)return false;
            for(int K=0;K<5;++K){const int B=ChainBoneIndices[SegmentIndex*5+K];if(B<SourceBoneCount || Helpers.Contains(B))return false;Helpers.Add(B);}
        }
        ++SegmentIndex;double Mass=0;
        for(const auto& N:S.Dynamics.Nodes) {if(N.BoneIndex<SourceBoneCount || Helpers.Contains(N.BoneIndex) || N.Rest.ContainsNaN() || N.Support.ContainsNaN() || N.MassKg<=0 || N.Support.GetMin()<=0 || N.PositiveTravel.GetMin()<=0) return false;Helpers.Add(N.BoneIndex);Mass+=N.MassKg;}
        if(!FMath::IsNearlyEqual(Mass,S.Dynamics.MassKg,1.e-6)) return false;
    }
    return Helpers.Num()==(SchemaVersion==3?44:24);
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

// Continuous Hermite muscle-support path. Anatomical main bones remain unchanged.
// Neighbor rotations affect endpoint tangents, never endpoint joint positions.
FTransform VamLegDynamics::ChainFrame(const FVamLegSegment& S,const TArray<FTransform>& Pose,double U,bool Enabled)
{
    U=FMath::Clamp(U,0.,1.);
    const FVector Rest(0,0,S.Length*(.5-U));
    if(!Enabled || S.ChainBones.Num()!=5)return FTransform(Rest);
    const int Primary=S.bCalf?S.Shin:S.Thigh,Parent=S.bCalf?S.Thigh:S.Pelvis,Distal=S.bCalf?S.Foot:S.Shin;
    const int J=S.bCalf?1:0;
    const FQuat Anchor=(S.AnchorLocal*Pose[Primary]).GetRotation();
    const FQuat PrimaryExpected=Pose[Parent].GetRotation()*S.JointRest[J].GetRotation();
    const FQuat DistalExpected=Pose[Primary].GetRotation()*S.JointRest[J+1].GetRotation();
    FQuat ProxDelta=Anchor.Inverse()*PrimaryExpected*Pose[Primary].GetRotation().Inverse()*Anchor;
    FQuat DistDelta=Anchor.Inverse()*Pose[Distal].GetRotation()*DistalExpected.Inverse()*Anchor;
    ProxDelta.Normalize();DistDelta.Normalize();
    const FVector D(0,0,-S.Length);
    const FVector A=FQuat::Slerp(FQuat::Identity,ProxDelta,.2).RotateVector(D);
    const FVector B=FQuat::Slerp(FQuat::Identity,DistDelta,.2).RotateVector(D);
    const FVector P0(0,0,S.Length*.5),P1=-P0;
    auto Point=[&](double T){return (2*T*T*T-3*T*T+1)*P0+(T*T*T-2*T*T+T)*A+(-2*T*T*T+3*T*T)*P1+(T*T*T-T*T)*B;};
    const FVector Tangent=(6*U*U-6*U)*P0+(3*U*U-4*U+1)*A+(-6*U*U+6*U)*P1+(3*U*U-2*U)*B;
    double Arc=0;FVector Last=P0;for(int K=1;K<=12;++K){const FVector Next=Point(K/12.);Arc+=(Next-Last).Size();Last=Next;}
    // Cross-sectional compensation is only an engineering volume approximation.
    const double Radial=FMath::Clamp(FMath::Sqrt(S.Length/FMath::Max(S.Length*.5,Arc)),.85,1.15);
    const double Fade=FMath::Square(FMath::Sin(PI*U));
    const FQuat Rotation=FQuat::FindBetweenNormals(FVector::ZAxisVector,(-Tangent).GetSafeNormal());
    return FTransform(Rotation,Point(U),FVector(1+(Radial-1)*Fade,1+(Radial-1)*Fade,1));
}

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVamLegChainTest,"Vam.Leg.LongitudinalChain",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FVamLegChainTest::RunTest(const FString&)
{
    FVamLegSegment S;S.Length=40;S.Thigh=1;S.Pelvis=0;S.Shin=2;S.Foot=3;S.JointRest.Init(FTransform::Identity,3);S.ChainBones={4,5,6,7,8};
    TArray<FTransform> P;P.Init(FTransform::Identity,4);
    for(int K=0;K<=20;++K){const double U=K/20.;const auto F=VamLegDynamics::ChainFrame(S,P,U,true);TestTrue(TEXT("Neutral preserves rest"),F.Equals(FTransform(FVector(0,0,40*(.5-U))),1.e-6));}
    P[2].SetRotation(FQuat(FVector::YAxisVector,PI*.65));
    TestTrue(TEXT("Flexion changes interior path"),!VamLegDynamics::ChainFrame(S,P,.5,true).Equals(FTransform::Identity,1.e-4));
    for(int K=0;K<=20;++K){const double U=K/20.;const auto Off=VamLegDynamics::ChainFrame(S,P,U,false);TestTrue(TEXT("Off is rigid parent equivalence"),Off.Equals(FTransform(FVector(0,0,40*(.5-U))),1.e-6));const auto On=VamLegDynamics::ChainFrame(S,P,U,true);TestFalse(TEXT("Finite output"),On.ContainsNaN());TestTrue(TEXT("Positive scale"),On.GetScale3D().GetMin()>0);}
    TestTrue(TEXT("Fixed proximal endpoint"),VamLegDynamics::ChainFrame(S,P,0,true).GetLocation().Equals(FVector(0,0,20),1.e-6));
    TestTrue(TEXT("Fixed distal endpoint"),VamLegDynamics::ChainFrame(S,P,1,true).GetLocation().Equals(FVector(0,0,-20),1.e-6));
    const FQuat World(FVector::XAxisVector,.7);auto Rotated=P;for(auto& F:Rotated)F.SetRotation(World*F.GetRotation());
    TestTrue(TEXT("Whole-body rotation invariant"),VamLegDynamics::ChainFrame(S,P,.4,true).Equals(VamLegDynamics::ChainFrame(S,Rotated,.4,true),1.e-6));
    return !HasAnyErrors();
}
#endif

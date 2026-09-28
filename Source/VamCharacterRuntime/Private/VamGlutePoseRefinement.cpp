#include "VamGluteStructure.h"
#include <cmath>

namespace
{
FVector RotationVector(FQuat Q)
{
    Q.Normalize();if(Q.W<0) Q=Q*-1.;
    FVector Axis;double Angle;Q.ToAxisAndAngle(Axis,Angle);return Axis*Angle;
}
double PositiveFeature(double Angle,double Range)
{
    const double X=FMath::Max(0.,Angle)/Range;return X*X/(1+X*X);
}
FQuat ExpRotation(const FVector& V)
{
    const double L=V.Size();return L>1.e-12?FQuat(V/L,L):FQuat::Identity;
}
}

FVamHipSidePose VamGluteStructure::HipSidePose(const FVamGluteSide& S,const FTransform& FemurInAnchor)
{
    FVamHipSidePose Out;Out.Side=S.Side;Out.FemurInAnchor=FemurInAnchor;
    Out.FemurInPelvis=FemurInAnchor*S.AnchorLocal;
    FQuat Delta=(FemurInAnchor.GetRotation()*S.RestThighInAnchor.GetRotation().Inverse()).GetNormalized();
    if(Delta.W<0) Delta=Delta*-1.;Out.RelativeOrientation=Delta;
    const FVector Axis=S.FemurAxisInAnchor.GetSafeNormal();
    const double Projected=FVector::DotProduct(FVector(Delta.X,Delta.Y,Delta.Z),Axis);
    FQuat Twist(Axis.X*Projected,Axis.Y*Projected,Axis.Z*Projected,Delta.W);
    if(Twist.SizeSquared()<1.e-12) Twist=FQuat::Identity;else Twist.Normalize();
    const FVector Swing=RotationVector(Delta*Twist.Inverse());
    Out.FlexionExtension=Swing.Y;Out.AbductionAdduction=S.SideSign*Swing.X;
    Out.ExternalInternalRotation=S.SideSign*FVector::DotProduct(RotationVector(Twist),Axis);
    return Out;
}
FVamHipPoseState VamGluteStructure::CaptureHipPose(const UVamGluteStructureProfile& P,const TArray<FVamGluteSide>& Rest,const FTransform& Pelvis,const FTransform& LeftFemur,const FTransform& RightFemur,int32 Revision)
{
    FVamHipPoseState State;State.ShapeRevision=Revision;State.PelvisComponent=Pelvis;
    State.LeftFemurComponent=LeftFemur;State.RightFemurComponent=RightFemur;
    if(Rest.Num()!=2) return State;
    const FQuat Reference=(Rest[0].AnchorLocal*P.RestPelvisComponent).GetRotation();
    const FVector Rotation=RotationVector(Reference.Inverse()*Pelvis.GetRotation()*P.RestPelvisComponent.GetRotation().Inverse()*Reference);
    State.PelvisTilt=Rotation.Y;State.PelvisYaw=Rotation.Z;State.PelvisRoll=Rotation.X;
    for(int32 I=0;I<2;++I)
    {
        const FTransform Anchor=Rest[I].AnchorLocal*Pelvis;
        State.Sides.Add(HipSidePose(Rest[I],(I==0?LeftFemur:RightFemur).GetRelativeTransform(Anchor)));
    }
    return State;
}

void VamGluteStructure::CalibratePoseRefinement(FVamGluteSide& S)
{
    // Shared semantic engineering policy; geometry and donor-derived attachments calibrate each instance.
    for(auto& R:S.Regions)
    {
        auto& C=R.PoseResponse;const double P=R.PelvisAttachment,T=R.ThighAttachment;
        C=FVamGlutePoseResponse();
        if(R.Semantic==TEXT("Upper"))
        {
            C.SupportGains=FVector4(2.4,.5,.4,.3);C.ThighFollow=.3;C.PelvisTether=2;
            C.FlexionOffset=FVector(.025,0,.018);C.ExtensionOffset=FVector(.012,0,.006);
            C.MaximumOffsetFraction=FVector(.07,.025,.05);C.MaximumDownwardFraction=.012;C.ProjectionRetention=.96;C.MaximumOrientationRadians=.16;
            C.OrientationGains=FVector(.025,.045,.02);
        }
        else if(R.Semantic==TEXT("Core"))
        {
            C.SupportGains=FVector4(1.8,.8,.55,.5);C.ThighFollow=.5;C.PelvisTether=1.5;
            C.FlexionOffset=FVector(.025,0,.012);C.ExtensionOffset=FVector(.02,0,-.006);
            C.MaximumOffsetFraction=FVector(.09,.035,.055);C.MaximumDownwardFraction=.025;C.ProjectionRetention=.93;C.MaximumOrientationRadians=.22;
            C.OrientationGains=FVector(.04,.07,.04);
        }
        else if(R.Semantic==TEXT("Lower"))
        {
            C.SupportGains=FVector4(.65,1.1,.5,.65);C.ThighFollow=1.25;C.PelvisTether=.5;
            C.FlexionOffset=FVector(.008,0,-.04);C.ExtensionOffset=FVector(.014,0,.035);
            C.AbductionOffset=FVector(0,.035,0);C.RotationOffset=FVector(.006,.025,0);
            C.MaximumOffsetFraction=FVector(.12,.10,.14);C.MaximumDownwardFraction=.10;C.ProjectionRetention=.78;C.MaximumOrientationRadians=.4;
            C.OrientationGains=FVector(.12,.13,.10);
        }
        else if(R.Semantic==TEXT("Medial"))
        {
            C.SupportGains=FVector4(2.2,.6,1.5,.8);C.ThighFollow=.15;C.PelvisTether=3;
            C.FlexionOffset=FVector(.006,0,.003);C.ExtensionOffset=FVector(.004,0,0);
            C.MaximumOffsetFraction=FVector(.025,.008,.02);C.MaximumDownwardFraction=.008;C.ProjectionRetention=.98;C.MaximumOrientationRadians=.08;
            C.OrientationGains=FVector(.015,.02,.012);
        }
        else if(R.Semantic==TEXT("Lateral"))
        {
            C.SupportGains=FVector4(.85,.8,1.8,1.8);C.ThighFollow=1;C.PelvisTether=.7;
            C.FlexionOffset=FVector(.014,0,-.01);C.ExtensionOffset=FVector(.01,0,.012);
            C.AbductionOffset=FVector(.015,.04,.015);C.RotationOffset=FVector(.012,.04,-.01);
            C.MaximumOffsetFraction=FVector(.10,.12,.08);C.MaximumDownwardFraction=.055;C.ProjectionRetention=.85;C.MaximumOrientationRadians=.35;
            C.OrientationGains=FVector(.15,.085,.16);
        }
        C.SupportGains*=.5+P;
        C.ThighFollow*=.75+T;C.PelvisTether*=.5+P;
    }
    if(S.Regions.Num()!=5) return;
    const auto& Lower=S.Regions[2];const auto& Medial=S.Regions[3];const auto& Lateral=S.Regions[4];
    auto& F=S.FoldSemanticMap;
    F.MedialInfraglutealAnchor=FVector(Lower.Rest.X,Medial.Rest.Y,Lower.Rest.Z);
    F.MiddleTransition=Lower.Rest;
    F.LateralFade=FVector(Lower.Rest.X,Lateral.Rest.Y,Lower.Rest.Z);
    F.ExtensionGains=FVector(1+Medial.PelvisAttachment,1+Lower.ThighAttachment,1+Lateral.ThighAttachment);
    F.FlexionStretchGains=FVector(Medial.PelvisAttachment,1+Lower.ThighAttachment,1+Lateral.ThighAttachment);
    F.AbductionGains=FVector(Medial.PelvisAttachment*.3,Lower.ThighAttachment,Lateral.ThighAttachment*2);
    F.RotationGains=FVector(Medial.ThighAttachment,Lower.ThighAttachment,Lateral.ThighAttachment*2);
}

void VamGluteStructure::RefinePose(const UVamGluteStructureProfile& P,const FVamGluteSide& S,FVamGluteStructuralState& Out)
{
    const auto& Hip=Out.HipPose;
    const double F=PositiveFeature(Hip.FlexionExtension,1),E=PositiveFeature(-Hip.FlexionExtension,.6);
    const double A=std::tanh(Hip.AbductionAdduction/.7),T=std::tanh(Hip.ExternalInternalRotation/.7);
    const double Activation=1-FMath::Exp(-(F+E+A*A+T*T));
    Out.HipAnglesDegrees=FVector(Hip.FlexionExtension,Hip.AbductionAdduction,Hip.ExternalInternalRotation)*(180./PI);
    Out.FinalRestCOM=FVector::ZeroVector;
    for(int32 I=0;I<S.Regions.Num();++I)
    {
        const auto& R=S.Regions[I];auto& N=Out.Regions[I];const auto& C=R.PoseResponse;
        const double Routing=C.SupportGains.X*F+C.SupportGains.Y*E+C.SupportGains.Z*A*A+C.SupportGains.W*T*T;
        // Bending of a routed fiber adds a passive strain proxy; no activation/force is inferred.
        N.Tension+=P.PassiveTensionGain*FMath::Square(Routing*.2);
        const double PP=R.PelvisAttachment*(1+N.Tension*R.PelvisAttachment+C.PelvisTether*Routing);
        const double TP=R.ThighAttachment*(1+N.Tension+C.ThighFollow*(E+A*A+T*T)*.3);
        N.PelvisAttachment=PP/(PP+TP);N.ThighAttachment=TP/(PP+TP);
        N.Support=R.SupportBaseline*(1+N.Tension+Routing);
        N.RegionalStiffnessBaseline=N.Support*FVector(1+C.ProjectionRetention*F,1+C.PelvisTether*A*A,1+C.SupportGains.X*F*.25);
        const double Follow=C.ThighFollow/(1+C.PelvisTether*F);
        const FVector FemurDelta=N.ThighPoint-S.RestThighInAnchor.TransformPosition(R.ThighPointLocal);
        FVector SemanticOffset=C.FlexionOffset*F+C.ExtensionOffset*E+C.AbductionOffset*A+C.RotationOffset*T;
        SemanticOffset.Y*=S.SideSign;
        FVector Offset=(N.Transform.GetLocation()-R.Rest)*Follow+FemurDelta*(N.ThighAttachment*Follow*.25)+SemanticOffset*S.Dimensions;
        for(int32 Axis=0;Axis<3;++Axis)
        {
            // Asymmetric SI travel limits meet with matching first derivative at zero.
            const double Fraction=Axis==2 && Offset.Z<0?C.MaximumDownwardFraction:C.MaximumOffsetFraction[Axis];
            const double Limit=FMath::Max(.001,S.Dimensions[Axis]*Fraction);
            Offset[Axis]=Limit*std::tanh(Offset[Axis]/Limit);
        }
        FVector Position=R.Rest+Offset;
        const double Retention=FMath::Max(R.PelvisAttachment,FMath::Lerp(R.PelvisAttachment,C.ProjectionRetention,Activation));
        const double Floor=R.Rest.X*Retention,Gap=FMath::Max(.001,R.Rest.X-Floor);
        Position.X=Floor+Gap*FMath::Exp(std::tanh((Position.X-R.Rest.X)/Gap));
        const FQuat Basis=FiberBasis(S,R);
        FVector Angular=RotationVector(N.Transform.GetRotation()*Basis.Inverse())*Follow;
        Angular+=FVector(S.SideSign*C.OrientationGains.X*A,C.OrientationGains.Y*(F-E),-S.SideSign*C.OrientationGains.Z*T);
        const double Length=Angular.Size();if(Length>1.e-12) Angular*=C.MaximumOrientationRadians*std::tanh(Length/C.MaximumOrientationRadians)/Length;
        N.OrientationAdjustment=ExpRotation(Angular);N.StructuralOffset=Position-R.Rest;
        // Preserve G0's det-one local scaffold scales; refine position and constrained orientation only.
        N.Transform.SetLocation(Position);N.Transform.SetRotation((N.OrientationAdjustment*Basis).GetNormalized());
        const FVector ShapedMass=N.Transform.TransformPosition(Basis.UnrotateVector(R.MassCenter-R.Rest));
        Out.FinalRestCOM+=ShapedMass*R.MassFractionCandidate;
    }
    const auto& M=S.FoldSemanticMap;FVector Factors;
    for(int32 I=0;I<3;++I)
    {
        const double Load=FMath::Max(0.,M.ExtensionGains[I]*E+M.AbductionGains[I]*A*A+M.RotationGains[I]*T*T-M.FlexionStretchGains[I]*F*.3);
        Factors[I]=1-FMath::Exp(-Load*Load);Out.FoldState.StretchState[I]=1-FMath::Exp(-M.FlexionStretchGains[I]*F);
    }
    Out.FoldState.MedialAnchorFactor=Factors.X;Out.FoldState.MiddleTransitionFactor=Factors.Y;Out.FoldState.LateralFadeFactor=Factors.Z;
}

#include "VamGluteJiggleProfile.h"

FVamGluteDynamicSide VamGluteDynamics::Calibrate(const UVamGluteJiggleProfile& P,const FVamGluteSide& S,const FVamGluteStructuralState& State)
{
    FVamGluteDynamicSide Out;Out.Side=S.Side;Out.COM=State.FinalRestCOM;Out.Dimensions=S.Dimensions;Out.MassKg=S.EffectiveVolumeCm3*P.DensityKgPerCm3;
    for(const auto& Imported:P.Sides) if(Imported.Side==S.Side) { Out.ReferenceGravityLocal=Imported.ReferenceGravityLocal;break; }
    if(S.Regions.Num()!=5 || State.Regions.Num()!=5) return Out;
    for(int32 I=0;I<S.Regions.Num();++I)
    {
        const auto& R=S.Regions[I];const auto& N=State.Regions[I];FVamGluteDynamicNode D;D.Semantic=R.Semantic;D.BoneIndex=R.BoneIndex;D.MassKg=Out.MassKg*R.MassFractionCandidate;
        D.Rest=N.Transform.GetLocation();D.COM=R.MassCenter;D.Inertia=R.InertiaCandidate;D.PelvisPoint=R.PelvisPoint;D.ThighPointLocal=R.ThighPointLocal;
        D.PelvisAttachment=N.PelvisAttachment;D.ThighAttachment=N.ThighAttachment;D.Support=N.RegionalStiffnessBaseline*P.DynamicModulusFraction;
        // Geometry + original structural tether calibrate travel; pose tension
        // changes stiffness above, not mass or the user's Mobility parameter.
        const double Freedom=(1+R.ThighAttachment)/(1+3*R.PelvisAttachment+2*R.PoseResponse.PelvisTether);
        D.PositiveTravel=S.Dimensions*(.12*Freedom);D.PositiveTravel.X=FMath::Min(D.PositiveTravel.X,FMath::Max(.05,R.Rest.X)*.3);
        D.NegativeTravel=D.PositiveTravel*FVector(.65,1,.8);
        for(int32 A=0;A<3;++A) { D.PositiveTravel[A]=FMath::Max(.02,D.PositiveTravel[A]);D.NegativeTravel[A]=FMath::Max(.02,D.NegativeTravel[A]); }
        if(P.bBilateralMaterialCalibration) for(const auto& Imported:P.Sides) if(Imported.Side==S.Side && Imported.Nodes.Num()==5)
        {
            const auto& Base=Imported.Nodes[I];
            // Preserve regional pose tension, Shape mass, and side-specific dimensions.
            D.Support=(Base.Support/Base.MassKg)*D.MassKg*(N.RegionalStiffnessBaseline/FMath::Max(1.e-8,R.SupportBaseline));
            D.PositiveTravel=Base.PositiveTravel/Imported.Dimensions*S.Dimensions;
            D.NegativeTravel=Base.NegativeTravel/Imported.Dimensions*S.Dimensions;
            break;
        }
        Out.Nodes.Add(D);
    }
    const int32 Graph[][2]={{0,1},{0,2},{0,3},{0,4},{1,3},{1,4},{2,3},{2,4}};
    for(const auto& Pair:Graph)
    {
        const auto& A=Out.Nodes[Pair[0]];const auto& B=Out.Nodes[Pair[1]];FVamGluteDynamicEdge E;E.A=Pair[0];E.B=Pair[1];
        const double Distance=(S.Regions[E.A].Rest-S.Regions[E.B].Rest).Size()/FMath::Max(.1,S.Dimensions.GetMin());
        const double Routing=(E.A==0?.18:.07)*FMath::Exp(-Distance)*(1-FMath::Abs(A.ThighAttachment-B.ThighAttachment)*.5);
        for(int32 Axis=0;Axis<3;++Axis) E.Stiffness[Axis]=FMath::Sqrt(A.Support[Axis]*B.Support[Axis])*Routing;
        Out.Couplings.Add(E);
    }
    return Out;
}
bool UVamGluteJiggleProfile::IsValidProfile() const
{
    if((SchemaVersion!=1 && SchemaVersion!=2) || Sides.Num()!=2 || SourceTopologyIdentity.IsEmpty() || SkeletonFamily.IsEmpty() || !FMath::IsFinite(DensityKgPerCm3) || DensityKgPerCm3<=0 || !FMath::IsFinite(FixedStep) || FixedStep<=0 || FixedStep>1./60. || MaxSubsteps<1 || MaxSubsteps>64 || SoftLimitFraction<=0 || SoftLimitFraction>=1) return false;
    if(SchemaVersion==2 && (AuthoredGravityWorld.ContainsNaN() || !FMath::IsNearlyEqual(AuthoredGravityWorld.Size(),980.,1.e-6) || Algorithm!=TEXT("glute-dual-attachment-g1.1-reference-gravity-v1") || GravityPolicy!=TEXT("body-attached-imported-1g-v1"))) return false;
    for(double Value:{DynamicModulusFraction,TeleportDistanceCm,TeleportAngleRadians,PoseDiscontinuityRadians,LargeShapeChangeRatio,SleepSpeedCmS,SleepDisplacementCm,SoftLimitFraction,NonlinearGain,LimitHardening}) if(!FMath::IsFinite(Value) || Value<=0) return false;
    if(SurfaceGuardVersion<0 || SurfaceGuardVersion>1) return false;
    if(SurfaceGuardVersion==1)
    {
        if(SurfaceGradients.IsEmpty() || !FMath::IsFinite(SurfaceGradientBudget) || SurfaceGradientBudget<=0 || SurfaceGradientBudget>=.5) return false;
        bool Seen[2]={false,false};
        for(const auto& G:SurfaceGradients)
        {
            if(G.Side<0 || G.Side>1 || G.WeightGradients.Num()!=5) return false;
            Seen[G.Side]=true;
            for(const auto& W:G.WeightGradients) if(!FMath::IsFinite(W.X) || !FMath::IsFinite(W.Y)) return false;
        }
        if(!Seen[0] || !Seen[1]) return false;
    }
    TSet<int32> Bones;
    for(const auto& S:Sides)
    {
        if(SchemaVersion==2 && (S.ReferenceGravityLocal.ContainsNaN() || !FMath::IsNearlyEqual(S.ReferenceGravityLocal.Size(),AuthoredGravityWorld.Size(),1.e-6))) return false;
        if(S.Nodes.Num()!=5 || S.Couplings.Num()!=8 || S.MassKg<=0 || !FMath::IsFinite(S.MassKg) || S.Dimensions.ContainsNaN()) return false;
        if(S.Dimensions.GetMin()<=0 || S.COM.ContainsNaN()) return false;
        const FName Semantics[]={TEXT("Core"),TEXT("Upper"),TEXT("Lower"),TEXT("Medial"),TEXT("Lateral")};
        for(int32 I=0;I<5;++I)
        {
            const auto& N=S.Nodes[I];
            if(N.Semantic!=Semantics[I] || Bones.Contains(N.BoneIndex) || N.COM.ContainsNaN() || N.Inertia.ContainsNaN() || N.Inertia.GetMin()<0 || N.PelvisPoint.ContainsNaN() || N.ThighPointLocal.ContainsNaN() || N.DampingRatio.ContainsNaN() || N.DampingRatio.GetMin()<=0 || !FMath::IsFinite(N.PelvisAttachment) || !FMath::IsFinite(N.ThighAttachment)) return false;
            Bones.Add(N.BoneIndex);
        }
        double Total=0;for(const auto& N:S.Nodes) { Total+=N.MassKg;if(N.BoneIndex<0 || N.Rest.ContainsNaN() || N.Support.ContainsNaN() || N.Support.GetMin()<=0 || N.PositiveTravel.ContainsNaN() || N.PositiveTravel.GetMin()<=0 || N.NegativeTravel.ContainsNaN() || N.NegativeTravel.GetMin()<=0 || N.MassKg<=0 || !FMath::IsFinite(N.MassKg) || N.PelvisAttachment<=0 || N.ThighAttachment<=0 || FMath::Abs(N.PelvisAttachment+N.ThighAttachment-1)>1.e-6) return false; }
        if(FMath::Abs(Total-S.MassKg)>1.e-7) return false;
        for(const auto& E:S.Couplings) if(E.A<0 || E.A>=5 || E.B<0 || E.B>=5 || E.A==E.B || E.Stiffness.ContainsNaN() || E.Stiffness.GetMin()<0) return false;
    }
    return true;
}

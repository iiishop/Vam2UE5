#include "VamGluteCorrectiveProfile.h"
#include "VamGluteStructureProfile.h"

bool UVamGluteCorrectiveProfile::IsValidProfile() const
{
    if((SchemaVersion!=1 && SchemaVersion!=2 && SchemaVersion!=3) || Targets.Num()<2 || Targets.Num()>32 || !Targets[0].Degrees.IsNearlyZero(1.e-12) || BuildDimensions.Num()!=2 || SourceTopologyIdentity.IsEmpty() || MetricDegrees.ContainsNaN() || MetricDegrees.GetMin()<=0 || !FMath::IsFinite(BlendWidth) || BlendWidth<=0 || !FMath::IsFinite(MaximumDimensionFraction) || MaximumDimensionFraction<=0 || MaximumDimensionFraction>.2 || Bases.IsEmpty()) return false;
    for(int32 I=0;I<Targets.Num();++I) { if(Targets[I].Degrees.ContainsNaN()) return false;for(int32 J=0;J<I;++J) if(Targets[I].Degrees.Equals(Targets[J].Degrees,1.e-6)) return false; }
    for(const FVector& D:BuildDimensions) if(D.ContainsNaN() || D.GetMin()<=0) return false;
    TSet<FName> Names;
    for(const auto& B:Bases)
    {
        if(B.Side<0 || B.Side>1 || B.Target<1 || B.Target>=Targets.Num() || B.Axis<0 || B.Axis>2 || B.Morph.IsNone() || Names.Contains(B.Morph) || !FMath::IsFinite(B.MaximumCm) || B.MaximumCm<=0 || B.RegionalRmsCm.Num()!=5 || B.DebugPositions.Num()!=B.DebugLocalDeltas.Num()) return false;
        Names.Add(B.Morph);
    }
    if(SchemaVersion>=2)
    {
        if(Diagnostics.Num()!=2*(Targets.Num()-1) || FidelityAuditJson.IsEmpty() || FamilyReferenceJson.IsEmpty()) return false;
        for(const auto& D:Diagnostics)
        {
            if(D.Side<0 || D.Side>1 || D.Target<1 || D.Target>=Targets.Num() || D.Positions.Num()!=D.Raw.Num() || D.Positions.Num()!=D.Adapted.Num() || D.Positions.Num()!=D.Procedural.Num() || D.Positions.Num()!=D.Final.Num()) return false;
            if(SchemaVersion>=3 && D.Positions.Num()!=D.SkinningResidual.Num()) return false;
            for(double X:{D.SkinningResidualRms,D.SourceRms,D.ProceduralRms,D.RawP95,D.AdaptedP95,D.FinalP95,D.AttenuationRatio,D.SafetyLoss,D.SmoothingLoss}) if(!FMath::IsFinite(X)) return false;
        }
    }
    return true;
}
TArray<double> VamGluteCorrective::Weights(const UVamGluteCorrectiveProfile& P,const FVamHipSidePose& Pose)
{
    TArray<double> Result;Result.Init(0,P.Targets.Num());if(Result.IsEmpty()) return Result;
    const FVector X=FVector(Pose.FlexionExtension,Pose.AbductionAdduction,Pose.ExternalInternalRotation)*(180./PI);
    if(X.ContainsNaN()) { Result[0]=1;return Result; }
    double Total=0;
    for(int32 I=0;I<P.Targets.Num();++I)
    {
        const double D=((X-P.Targets[I].Degrees)/P.MetricDegrees).SizeSquared();
        if(D<1.e-20) { Result.Init(0,P.Targets.Num());Result[I]=1;return Result; }
        Result[I]=FMath::Exp(FMath::Max(-80.,-D/(2*P.BlendWidth*P.BlendWidth)))/(D*D);Total+=Result[I];
    }
    if(Total<=0 || !FMath::IsFinite(Total)) { Result.Init(0,P.Targets.Num());Result[0]=1;return Result; }
    for(double& W:Result) W/=Total;
    return Result;
}
FVector VamGluteCorrective::ShapeScale(const FVector& Build,const FVector& Current)
{
    FVector Scale;
    // Match ordinary proportions exactly; continuously bound extreme extrapolation.
    for(int32 A=0;A<3;++A) Scale[A]=FMath::Clamp(Current[A]/FMath::Max(.01,Build[A]),.25,4.);
    return Scale;
}

FVector VamGluteCorrective::CurvatureDelta(const FVamGluteSide& S,const FVector& Point,const FVector& Ang,const FVamGluteFoldState& FoldState)
{
    auto Bell=[](double X){return FMath::Exp(-X*X*2);};
    const double Flex=FMath::Max(0.,Ang.X)/90.,Ext=FMath::Max(0.,-Ang.X)/20.,Abd=Ang.Y/35.,Twist=Ang.Z/30.;
    const FVector Q=(Point-S.Regions[2].Rest)/S.Dimensions;
    const double Across=S.SideSign*(Point.Y-S.FoldSemanticMap.MedialInfraglutealAnchor.Y)/FMath::Max(.1,FMath::Abs(S.FoldSemanticMap.LateralFade.Y-S.FoldSemanticMap.MedialInfraglutealAnchor.Y));
    const double Lower=Bell(Q.Z/.19)*Bell(Q.Y/.48),Core=Bell((Point.Z-S.Regions[0].Rest.Z)/FMath::Max(.1,S.Dimensions.Z*.32));
    const double Curve=(1-2*FMath::Square(Q.Z/.19))*Lower;
    const double Outer=FMath::SmoothStep(.25,1.,Across);
    const double Fold=FoldState.MiddleTransitionFactor*(1-Outer)+FoldState.LateralFadeFactor*Outer*Outer;
    // Curvature lobes redistribute contour; no extra scaffold rigid translation/rotation.
    return FVector(S.Dimensions.X*(.085*Flex*Flex*Curve+.025*Ext*Curve+.035*Abd*Outer*Core+.035*Twist*Outer*Lower+.02*Flex*Abd*Curve-.04*Fold*Lower),
        S.SideSign*S.Dimensions.Y*(.035*Abd*Outer*Lower+.025*Twist*Outer*Curve),
        S.Dimensions.Z*(.025*Flex*Lower*Q.Z/.19-.025*Ext*Lower*Q.Z/.19+.015*Twist*Outer*Curve));
}

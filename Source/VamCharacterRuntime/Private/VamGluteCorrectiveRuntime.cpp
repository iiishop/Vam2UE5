#include "VamGluteSkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/MorphTarget.h"
#include "AnimationRuntime.h"
#include "DrawDebugHelpers.h"

void UVamGluteSkeletalMeshComponent::ApplyGluteCorrectives()
{
    if(!CorrectiveProfile || !CorrectiveProfile->IsValidProfile() || !GetSkeletalMeshAsset() || HipPoseState.Sides.Num()!=2 || GluteRest.Num()!=2) return;
    CorrectiveTargetWeights.Reset();CorrectiveRegionalBounds.Init(0,10);CorrectiveMagnitudeBound=0;
    for(const auto& Side:HipPoseState.Sides) CorrectiveTargetWeights.Add(VamGluteCorrective::Weights(*CorrectiveProfile,Side));
    for(const auto& Basis:CorrectiveProfile->Bases)
    {
        const auto& S=GluteRest[Basis.Side];
        const FVector Scale=VamGluteCorrective::ShapeScale(CorrectiveProfile->BuildDimensions[Basis.Side],S.Dimensions);
        const double W=bCorrectiveEnabled?CorrectiveTargetWeights[Basis.Side][Basis.Target]*Scale[Basis.Axis]:0;
        CorrectiveWeights.Add(Basis.Morph,W);SetMorphTarget(Basis.Morph,W,false);
        CorrectiveMagnitudeBound+=FMath::Abs(W)*Basis.MaximumCm;
        for(int32 N=0;N<5;++N) CorrectiveRegionalBounds[Basis.Side*5+N]+=FMath::Abs(W)*Basis.RegionalRmsCm[N];
        if(CorrectiveProfile->SchemaVersion==1 && bShowCorrectiveDelta && GetWorld() && W>.001)
        {
            const FTransform World=S.AnchorLocal*HipPoseState.PelvisComponent*GetComponentTransform();
            for(int32 I=0;I<Basis.DebugPositions.Num();++I)
            {
                const FVector Start=World.TransformPosition(Basis.DebugPositions[I]);
                DrawDebugLine(GetWorld(),Start,Start+World.TransformVectorNoScale(Basis.DebugLocalDeltas[I]*W),FColor::Orange,false,0,0,.6f);
            }
        }
    }
    if(bShowCorrectiveDelta && GetWorld()) for(const auto& D:CorrectiveProfile->Diagnostics)
    {
        if(D.Target!=CorrectiveDiagnosticTarget || (DebugGluteSide>=0 && D.Side!=DebugGluteSide)) continue;
        const auto& Vectors=CorrectiveDiagnosticStage==0?D.Raw:CorrectiveDiagnosticStage==1?D.Adapted:CorrectiveDiagnosticStage==2?D.Procedural:CorrectiveDiagnosticStage==4?D.SkinningResidual:D.Final;
        const FColor Colors[]={FColor::Cyan,FColor::Green,FColor::Yellow,FColor::Orange,FColor::Magenta};
        const FTransform World=GluteRest[D.Side].AnchorLocal*HipPoseState.PelvisComponent*GetComponentTransform();
        for(int32 V=0;V<D.Positions.Num() && V<Vectors.Num();++V) { const FVector Start=World.TransformPosition(D.Positions[V]);DrawDebugLine(GetWorld(),Start,Start+World.TransformVectorNoScale(Vectors[V]),Colors[FMath::Clamp(CorrectiveDiagnosticStage,0,4)],false,0,0,.6f); }
    }
    // Finalize runs after primary animation/rigid completion, before native render publication.
    // Refresh native curve buffers now, rather than waiting for next TickAnimation's curve cache.
    // This preserves animation/Shape curves; no render-resource rebuild and no CPU mesh skinning.
    FAnimationRuntime::AppendActiveMorphTargets(GetSkeletalMeshAsset(),CorrectiveWeights,ActiveMorphTargets,MorphTargetWeights);MarkRenderDynamicDataDirty();
}
float UVamGluteSkeletalMeshComponent::AppliedCorrectiveWeight(FName Morph) const
{
    if(!GetSkeletalMeshAsset()) return 0;
    const auto* Target=GetSkeletalMeshAsset()->FindMorphTarget(Morph);
    const int32 Index=GetSkeletalMeshAsset()->GetMorphTargets().IndexOfByKey(Target);
    return MorphTargetWeights.IsValidIndex(Index)?MorphTargetWeights[Index]:0;
}

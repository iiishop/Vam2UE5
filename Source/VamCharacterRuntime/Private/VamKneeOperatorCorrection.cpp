#include "VamLegSkeletalMeshComponent.h"
#include "VamCharacterComponent.h"
#include "VamRuntimeConfiguration.h"
#include "VamBreastContactProfile.h"
#include "VamGPUContactDataInterface.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/MorphTarget.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/SkinWeightVertexBuffer.h"
#include "GameFramework/Actor.h"
#include "BoneContainer.h"

void UVamLegSkeletalMeshComponent::UpdateKneeOperatorCorrection()
{
 const double Start=FPlatformTime::Seconds();
 auto* Mesh=GetSkeletalMeshAsset();
 KneeOperatorMicroseconds=0;KneeHelperDifference.Reset();
 if(!Mesh||!bCorrectiveEnabled||!bKneeOperatorCompensation||!bLegChainEnabled||LegChainMethod!=4||LegRest.Num()!=4){KneeSkinCorrection.Reset();VamGPUSetSkinCorrection(this,{});return;}
 const auto* Render=Mesh->GetResourceForRendering();if(!Render||Render->LODRenderData.IsEmpty())return;
 const auto& L=Render->LODRenderData[0];auto* Weights=GetSkinWeightBuffer(0);if(!Weights||!Weights->GetDataVertexBuffer()->GetWeightData())return;
 const auto& Pose=GetComponentSpaceTransforms();
 const auto& Inv=GetRefPoseOverride()?GetRefPoseOverride()->RefBasesInvMatrix:Mesh->GetRefBasesInvMatrix();
 TArray<FMatrix> Difference;Difference.Init(FMatrix(ForceInitToZero),Pose.Num());
 KneeHelperDifference.Init(FMatrix(ForceInitToZero),Pose.Num());
 for(const auto& S:LegRest){
  const FTransform Anchor=S.AnchorLocal*Pose[S.bCalf?S.Shin:S.Thigh];
  auto Add=[&](int B,const FTransform& Ref){if(Pose.IsValidIndex(B)&&Inv.IsValidIndex(B)){FMatrix D=Ref.ToMatrixWithScale(),Current=Pose[B].ToMatrixWithScale();for(int R=0;R<4;++R)for(int C=0;C<4;++C)D.M[R][C]-=Current.M[R][C];KneeHelperDifference[B]=D;Difference[B]=FMatrix(Inv[B])*D;}};
  for(int K=0;K<S.ChainBones.Num();++K)Add(S.ChainBones[K],FTransform(FVector(0,0,S.Length*(.5-K/4.)))*Anchor);
  for(const auto& N:S.Dynamics.Nodes)Add(N.BoneIndex,FTransform(N.Rest)*Anchor);
 }
 // Only knee corrective directions are transported through the reference
 // operator. M's base shape, translations and all Jiggle offsets stay intact.
 TArray<FVector3f> Delta;Delta.Init(FVector3f::ZeroVector,L.GetNumVertices());
 for(const auto& Pair:ActiveMorphTargets)if(Pair.Key&&Pair.Key->GetName().StartsWith(TEXT("Knee_"))&&MorphTargetWeights.IsValidIndex(Pair.Value)){
  const float W=MorphTargetWeights[Pair.Value];if(FMath::Abs(W)<1.e-8f)continue;
  for(const auto& V:Pair.Key->GetMorphTargetDeltas(0))if(Delta.IsValidIndex(V.SourceIdx))Delta[V.SourceIdx]+=V.PositionDelta*W;
 }
 KneeSkinCorrection.Init(FVector4f(0,0,0,0),Delta.Num());
 for(const auto& Section:L.RenderSections)for(uint32 V=Section.BaseVertexIndex;V<Section.BaseVertexIndex+Section.NumVertices;++V){
  if(Delta[V].IsNearlyZero(1.e-9f))continue;FVector D=FVector::ZeroVector;
  for(uint32 K=0;K<Weights->GetMaxBoneInfluences();++K)if(const uint16 W=Weights->GetBoneWeight(V,K)){
   const int B=Section.BoneMap[Weights->GetBoneIndex(V,K)];D+=FVector(Difference[B].TransformVector(FVector(Delta[V])))*(W/65535.);
  }
  if(D.ContainsNaN()){KneeSkinCorrection.Reset();VamGPUSetSkinCorrection(this,{});return;}
  KneeSkinCorrection[V]=FVector4f(FVector3f(D),0);
 }
 VamGPUSetSkinCorrection(this,TArray<FVector4f>(KneeSkinCorrection));
 // Install the same native Optimus skin/normal graph even with contact OFF.
 // Its contact input is zero in that case; no physics solver is allocated.
 if(auto* Character=GetOwner()->FindComponentByClass<UVamCharacterComponent>())if(auto* Config=Character->GetLoadedRuntimeConfiguration())if(auto* Contact=Config->BreastContact.Get())
  if(Contact->GPUSurfaceDeformer&&GetComponentMeshDeformer()!=Contact->GPUSurfaceDeformer)SetMeshDeformer(Contact->GPUSurfaceDeformer);
 KneeOperatorMicroseconds=(FPlatformTime::Seconds()-Start)*1.e6;
}

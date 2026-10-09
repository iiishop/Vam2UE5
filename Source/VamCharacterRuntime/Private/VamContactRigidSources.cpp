#include "VamContactRigidSources.h"
#include "VamBreastSkeletalMeshComponent.h"
#include "VamBreastContactComponent.h"
#include "VamShapeAnimInstance.h"
#include "VamCharacterComponent.h"
#include "VamRuntimeConfiguration.h"
#include "Components/PrimitiveComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/SkeletalBodySetup.h"
#include "GameFramework/Actor.h"
#include "PhysicsEngine/BodyInstance.h"

void VamGatherRigidSources(UPrimitiveComponent* Source,UVamBreastSkeletalMeshComponent* OwnBody,const FBox& Bounds,TArray<FVamContactRigidSource>& Out)
{
 if(!Source || !OwnBody || !Source->IsCollisionEnabled() || Source->GetCollisionResponseToChannel(OwnBody->GetCollisionObjectType())!=ECR_Block)return;
 if(auto* Mesh=Cast<USkeletalMeshComponent>(Source)){
  auto* Asset=Mesh->GetPhysicsAsset();if(!Asset)return;TSet<int> Excluded,OwnDistalRoots;
  if(Mesh==OwnBody)if(auto* Anim=Cast<UVamShapeAnimInstance>(Mesh->GetAnimInstance()))for(const auto& J:Anim->GetRigJoints()){
   const auto Semantic=J.Semantic.ToString();if(Semantic.EndsWith(TEXT("_elbow"))||Semantic.EndsWith(TEXT("_hand"))||Semantic.EndsWith(TEXT("_knee"))||Semantic.EndsWith(TEXT("_foot"))||Semantic==TEXT("head"))OwnDistalRoots.Add(Mesh->GetBoneIndex(J.Bone));}
  auto* Soft=Source->GetOwner()?Source->GetOwner()->FindComponentByClass<UVamBreastContactComponent>():nullptr;
  if(auto* Breast=Cast<UVamBreastSkeletalMeshComponent>(Mesh))if(Mesh==OwnBody || (Soft&&Soft->bEnabled&&Soft->bUseGPU))for(const auto& Side:Breast->RestSides){
   int Bone=Side.ChestBone;while(Bone>=0){Excluded.Add(Bone);Bone=Mesh->GetBoneIndex(Mesh->GetParentBone(Mesh->GetBoneName(Bone)));}}
  // The legacy PhysicsAsset includes rigid pectoral/nipple bodies. Once this
  // region is represented by a volume, those proxies must not collide a second
  // time. Recover region donors from authored skin evidence, not bone names.
  if(Soft && Soft->bEnabled && Soft->bUseGPU)if(auto* Character=Source->GetOwner()->FindComponentByClass<UVamCharacterComponent>())if(auto* Config=Character->RuntimeConfiguration.Get())if(auto* Profile=Config->BreastContact.Get()){
   TSet<int> Articulated;
   if(auto* Anim=Cast<UVamShapeAnimInstance>(Mesh->GetAnimInstance()))for(const auto& Joint:Anim->GetRigJoints())Articulated.Add(Mesh->GetBoneIndex(Joint.Bone));
   for(const auto& Particle:Profile->Particles)if(!Particle.bKinematic && (Particle.Side<2 || Soft->bLowerBodyContactEnabled))for(int J=0;J<Particle.Bones.Num();++J)if(Particle.Weights.IsValidIndex(J)&&Particle.Weights[J]>0&&(!Articulated.Contains(Particle.Bones[J]) || Particle.Side>=2))Excluded.Add(Particle.Bones[J]);
  }
  for(int I=0;I<Asset->SkeletalBodySetups.Num();++I){auto* Setup=Asset->SkeletalBodySetups[I].Get();if(!Setup)continue;const auto* BI=Mesh->GetBodyInstance(Setup->BoneName);if(!BI || BI->GetCollisionEnabled()==ECollisionEnabled::NoCollision)continue;int Bone=Mesh->GetBoneIndex(Setup->BoneName);if(Bone<0||Excluded.Contains(Bone))continue;
   if(Mesh==OwnBody){bool Allowed=false;int Parent=Bone;while(Parent>=0){if(OwnDistalRoots.Contains(Parent)){Allowed=true;break;}Parent=Mesh->GetBoneIndex(Mesh->GetParentBone(Mesh->GetBoneName(Parent)));}if(!Allowed)continue;}
   const FTransform World=Mesh->GetBoneTransform(Bone);if(Setup->AggGeom.CalcAABB(World).Intersect(Bounds))Out.Add({Source,Setup,World,Setup->BoneName,uint32(I+1)});}
 }else if(auto* Setup=Source->GetBodySetup())Out.Add({Source,Setup,Source->GetComponentTransform(),NAME_None,0});
}
bool VamSupportsGPURigid(const FVamContactRigidSource& S)
{
 if(!S.Setup || S.Component->IsA<UInstancedStaticMeshComponent>())return false;
 const FVector Scale=S.World.GetScale3D();if(Scale.GetMin()<=0)return false;
 if(Scale.GetMax()-Scale.GetMin()>.001){const auto& G=S.Setup->AggGeom;if(G.BoxElems.IsEmpty()||G.GetElementCount()!=G.BoxElems.Num())return false;for(const auto& B:G.BoxElems)if(!B.Rotation.IsNearlyZero())return false;}
 const auto& G=S.Setup->AggGeom;
 if(G.GetElementCount()==0 || G.GetElementCount()!=G.SphereElems.Num()+G.BoxElems.Num()+G.SphylElems.Num()+G.ConvexElems.Num())return false;
 for(const auto& C:G.ConvexElems){if(C.GetTransform().GetScale3D().GetMin()<=0)return false;TArray<FPlane> Planes;C.GetPlanes(Planes);if(Planes.Num()<4)return false;for(const auto& P:Planes)if(P.ContainsNaN() || !FMath::IsFinite(P.W) || FVector(P.X,P.Y,P.Z).SizeSquared()<1e-16)return false;}
 return true;
}

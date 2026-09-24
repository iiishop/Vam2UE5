#include "VamInteractionComponent.h"
#include "VamCharacterComponent.h"
#include "PhysicsEngine/PhysicsHandleComponent.h"
#include "PhysicsEngine/PhysicalAnimationComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "PhysicsEngine/BodyInstance.h"

UVamInteractionComponent::UVamInteractionComponent()
{
    PrimaryComponentTick.bCanEverTick=false;
}
void UVamInteractionComponent::BeginPlay()
{
    Super::BeginPlay();
    Handle=GetOwner()->FindComponentByClass<UPhysicsHandleComponent>();
    PhysicalAnimation=GetOwner()->FindComponentByClass<UPhysicalAnimationComponent>();
}
bool UVamInteractionComponent::SetPhysicalMode(EVamPhysicalMode NewMode, FName LocalRootBone)
{
    auto* Character=GetOwner()->FindComponentByClass<UVamCharacterComponent>();
    USkeletalMeshComponent* Body=Character ? Character->Body : nullptr;
    if (!Body || !Body->GetPhysicsAsset()) return false;
    if (!GrabbedBone.IsNone()) ReleaseGrab();
    Body->SetEnableGravity(bGravity);
    Body->SetCollisionEnabled(NewMode==EVamPhysicalMode::Controlled ? ECollisionEnabled::QueryOnly : ECollisionEnabled::QueryAndPhysics);
    if (NewMode==EVamPhysicalMode::Controlled)
    {
        Body->SetSimulatePhysics(false);
        Body->SetAllBodiesPhysicsBlendWeight(0.f);
    }
    else if (NewMode==EVamPhysicalMode::Ragdoll)
    {
        Body->SetSimulatePhysics(true);
        Body->SetAllBodiesPhysicsBlendWeight(1.f);
    }
    else
    {
        if (LocalRootBone.IsNone() || Body->GetBoneIndex(LocalRootBone)==INDEX_NONE) return false;
        Body->SetSimulatePhysics(false);
        Body->SetAllBodiesBelowSimulatePhysics(LocalRootBone,true,true);
        Body->SetAllBodiesBelowPhysicsBlendWeight(LocalRootBone,FMath::Clamp(PhysicsBlend,0.f,1.f),false,true);
        if (PhysicalAnimation)
        {
            PhysicalAnimation->SetSkeletalMeshComponent(Body);
            FPhysicalAnimationData Drive;
            Drive.bIsLocalSimulation=true;
            Drive.OrientationStrength=DriveStrength;
            Drive.AngularVelocityStrength=DriveDamping;
            Drive.PositionStrength=DriveStrength;
            Drive.VelocityStrength=DriveDamping;
            PhysicalAnimation->ApplyPhysicalAnimationSettingsBelow(LocalRootBone,Drive,true);
        }
    }
    LocalRoot=LocalRootBone;
    Mode=NewMode;
    return true;
}
bool UVamInteractionComponent::SetRootMotionResponse(FName LowerRootBone, FName UpperRootBone)
{
    auto* Character=GetOwner()->FindComponentByClass<UVamCharacterComponent>();
    USkeletalMeshComponent* Body=Character ? Character->Body : nullptr;
    if (!Body || !Body->GetPhysicsAsset() || LowerRootBone.IsNone() || UpperRootBone.IsNone() ||
        LowerRootBone==UpperRootBone || !Body->GetBodyInstance(LowerRootBone) || !Body->GetBodyInstance(UpperRootBone)) return false;
    if (!GrabbedBone.IsNone()) ReleaseGrab();
    Body->SetEnableGravity(bGravity);
    Body->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    Body->SetSimulatePhysics(false);
    const float Blend=FMath::Clamp(PhysicsBlend,0.f,1.f);
    for (const FName Root : {LowerRootBone,UpperRootBone})
    {
        Body->SetAllBodiesBelowSimulatePhysics(Root,true,true);
        Body->SetAllBodiesBelowPhysicsBlendWeight(Root,Blend,false,true);
    }
    if (PhysicalAnimation)
    {
        PhysicalAnimation->SetSkeletalMeshComponent(Body);
        FPhysicalAnimationData Drive;
        Drive.bIsLocalSimulation=true;
        Drive.OrientationStrength=DriveStrength;
        Drive.AngularVelocityStrength=DriveDamping;
        Drive.PositionStrength=DriveStrength;
        Drive.VelocityStrength=DriveDamping;
        for (const FName Root : {LowerRootBone,UpperRootBone})
            PhysicalAnimation->ApplyPhysicalAnimationSettingsBelow(Root,Drive,true);
    }
    LocalRoot=NAME_None;
    Mode=EVamPhysicalMode::LocalResponse;
    return true;
}
bool UVamInteractionComponent::GrabBone(FName Bone, FVector WorldLocation)
{
    auto* Character=GetOwner()->FindComponentByClass<UVamCharacterComponent>();
    USkeletalMeshComponent* Body=Character ? Character->Body : nullptr;
    if (!Body || !Handle || !Body->GetPhysicsAsset() || Body->GetBoneIndex(Bone)==INDEX_NONE) return false;
    // A hand or foot body alone cannot move against a locked joint to a kinematic parent.
    // Simulate a short ancestor chain so the handle can bend the limb while the torso stays controlled.
    FName SimulationRoot=Bone;
    for (int32 Ancestor=0;Ancestor<2;++Ancestor)
    {
        const FName Parent=Body->GetParentBone(SimulationRoot);
        if (Parent.IsNone() || !Body->GetBodyInstance(Parent)) break;
        SimulationRoot=Parent;
    }
    if (Mode!=EVamPhysicalMode::Ragdoll &&
        (Mode!=EVamPhysicalMode::LocalResponse || LocalRoot!=SimulationRoot) &&
        !SetPhysicalMode(EVamPhysicalMode::LocalResponse,SimulationRoot)) return false;
    const FBodyInstance* BoneBody=Body->GetBodyInstance(Bone);
    if (!BoneBody || !BoneBody->IsInstanceSimulatingPhysics()) return false;
    Handle->LinearStiffness=FMath::Max(1.f,DriveStrength);
    Handle->LinearDamping=FMath::Max(1.f,DriveDamping);
    Handle->AngularStiffness=FMath::Max(1.f,DriveStrength);
    Handle->AngularDamping=FMath::Max(1.f,DriveDamping);
    Handle->GrabComponentAtLocationWithRotation(Body,Bone,WorldLocation,Body->GetBoneQuaternion(Bone).Rotator());
    GrabbedBone=Bone;
    return true;
}
void UVamInteractionComponent::MoveGrab(FVector WorldLocation)
{
    if (Handle && !GrabbedBone.IsNone()) Handle->SetTargetLocation(WorldLocation);
}
void UVamInteractionComponent::ReleaseGrab()
{
    if (Handle) Handle->ReleaseComponent();
    GrabbedBone=NAME_None;
}

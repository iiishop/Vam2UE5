#include "VamBodyContactResponseComponent.h"
#include "VamCharacterComponent.h"
#include "VamPhysicsOutputComponent.h"
#include "VamMotionComponent.h"
#include "VamBreastContactComponent.h"
#include "VamShapeAnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "PhysicsEngine/BodyInstance.h"
#include "Engine/World.h"
#include "Engine/OverlapResult.h"

UVamBodyContactResponseComponent::UVamBodyContactResponseComponent()
{
    PrimaryComponentTick.bCanEverTick=true;PrimaryComponentTick.TickGroup=TG_PrePhysics;
}
void UVamBodyContactResponseComponent::AddContactForce(FVector Force,FVector Point)
{
    if(bEnabled && !Force.ContainsNaN() && !Point.ContainsNaN())Pending.Add({Force.GetClampedToMaxSize(FMath::Max(0.f,MaximumForceNewtons)),Point});
}
void UVamBodyContactResponseComponent::AddContactWrench(FVector Force,FVector Torque,FVector Origin)
{
    if(!bEnabled || Force.ContainsNaN() || Torque.ContainsNaN() || Origin.ContainsNaN())return;
    const double Scale=FMath::Min(1.,FMath::Max(0.f,MaximumForceNewtons)/FMath::Max(Force.Size(),1e-9));
    Pending.Add({Force*Scale,Origin,Torque*Scale});
}
void UVamBodyContactResponseComponent::ResetResponse()
{
    Pending.Reset();Offsets.Reset();PreviousCenters.Reset();Position=Velocity=Angle=AngularVelocity=FVector::ZeroVector;AppliedForceNewtons=FVector::ZeroVector;
}
void UVamBodyContactResponseComponent::TickComponent(float Dt,ELevelTick Tick,FActorComponentTickFunction* Function)
{
    Super::TickComponent(Dt,Tick,Function);
    auto* C=GetOwner()->FindComponentByClass<UVamCharacterComponent>();auto* B=C?C->Body.Get():nullptr;
    auto* A=B?Cast<UVamShapeAnimInstance>(B->GetAnimInstance()):nullptr;
    if(!bEnabled || !A){ResetResponse();Status=TEXT("Body response disabled / character unavailable");return;}
    if(Dt<=0 || GetWorld()->IsPaused())return;
    auto* M=GetOwner()->FindComponentByClass<UVamMotionComponent>();const int32 Teleport=M?M->GetClock().TeleportRevision:0;if(M && M->GetClock().bPaused){Pending.Reset();return;}
    if(LastBody.Get()!=B || LastShape!=C->GetShapeState().Revision || LastTeleport!=Teleport){ResetResponse();LastBody=B;LastShape=C->GetShapeState().Revision;LastTeleport=Teleport;B->AddTickPrerequisiteComponent(this);}
    const auto* Joint=A->GetRigJoints().FindByPredicate([](const FVamRigJoint& J){return J.Semantic==FName(TEXT("chest"));});
    const int32 Bone=Joint?B->GetBoneIndex(Joint->Bone):INDEX_NONE;
    if(Bone<0){ResetResponse();Status=TEXT("Unsupported rig: chest semantic absent");return;}
    const FName BoneName=Joint->Bone;const FTransform BoneWorld=B->GetBoneTransform(Bone);
    const int32 Parent=B->GetBoneIndex(B->GetParentBone(BoneName));const FTransform ParentWorld=Parent>=0?B->GetBoneTransform(Parent):B->GetComponentTransform();
    float Mass=15; if(auto* BI=B->GetBodyInstance(BoneName))Mass=FMath::Clamp(BI->GetBodyMass(),1.f,100.f);
    float LeverCm=FMath::Max(5.f,float(FVector::Distance(BoneWorld.GetLocation(),ParentWorld.GetLocation())));
    // Outside the soft layer this proxy remains available. Volumetric-enabled chest
    // contacts use their own force bridge to avoid applying the same load twice.
    const auto* Soft=GetOwner()->FindComponentByClass<UVamBreastContactComponent>();
    const bool UseProxy=bRigidProxyContact;
    if(UseProxy) { if(auto* Output=GetOwner()->FindComponentByClass<UVamPhysicsOutputComponent>())
    {
        const auto Collision=Output->GetCollisionOutput();int32 Index=0;
        if(Collision.bValid)for(const auto& Capsule:Collision.Capsules)
        {
            const int32 Key=Index++;if(Capsule.Bone!=BoneName)continue;
            const FVector End=Capsule.WorldTransform.GetLocation();const FVector Start=PreviousCenters.Contains(Key)?PreviousCenters[Key]:End;PreviousCenters.Add(Key,End);
            TArray<FHitResult> Hits;FCollisionObjectQueryParams Objects;Objects.AddObjectTypesToQuery(ECC_WorldStatic);Objects.AddObjectTypesToQuery(ECC_WorldDynamic);Objects.AddObjectTypesToQuery(ECC_PhysicsBody);
            FCollisionQueryParams Query(SCENE_QUERY_STAT(VamBodyResponse),false,GetOwner());Query.bFindInitialOverlaps=true;
            GetWorld()->SweepMultiByObjectType(Hits,Start,End,Capsule.WorldTransform.GetRotation(),Objects,FCollisionShape::MakeCapsule(Capsule.Radius,Capsule.HalfLength+Capsule.Radius),Query);
            for(const auto& Hit:Hits)if(Hit.bBlockingHit)
            {
                if(Hit.GetComponent() && Hit.GetComponent()->GetCollisionResponseToChannel(B->GetCollisionObjectType())!=ECR_Block)continue;
                if(Soft && Soft->SuppliesContactForceFor(Hit.GetComponent()))continue;
                const FVector Normal=Hit.ImpactNormal.GetSafeNormal();const float Closing=FMath::Max(0.f,float(-FVector::DotProduct((End-Start)/FMath::Max(Dt,.001f),Normal)));
                const float Depth=Hit.bStartPenetrating?Hit.PenetrationDepth:float(FVector::Distance(Hit.Location,End));
                const FVector Force=Normal*FMath::Min(MaximumForceNewtons,FMath::Max(0.f,ProxyStiffnessNewtonsPerCm)*FMath::Max(0.f,Depth)+Closing*.2f);
                const FVector Point=Hit.ImpactPoint;Pending.Add({Force,Point});
                if(auto* Other=Hit.GetComponent())if(Other->IsSimulatingPhysics(Hit.BoneName))Other->AddForceAtLocation(-Force*100,Point,Hit.BoneName);
            }
        }
    }
    } else PreviousCenters.Reset();
    FVector Force=FVector::ZeroVector,Torque=FVector::ZeroVector;
    for(const auto& L:Pending){Force+=L.Force;Torque+=L.Torque+FVector::CrossProduct((L.Point-ParentWorld.GetLocation())*.01,L.Force);}Pending.Reset();
    const double WrenchScale=FMath::Min(1.,FMath::Max(0.f,MaximumForceNewtons)/FMath::Max(Force.Size(),1e-9));Force*=WrenchScale;Torque*=WrenchScale;AppliedForceNewtons=Force;
    if(B->IsSimulatingPhysics(BoneName)){B->AddForceAtLocation(Force*100,ParentWorld.GetLocation(),BoneName);B->AddTorqueInRadians(Torque*10000,BoneName);Offsets.Reset();Status=TEXT("Body force routed to simulated rigid skeleton");return;}
    const FVector LocalForce=ParentWorld.InverseTransformVectorNoScale(Force)*100;
    const FVector LocalTorque=ParentWorld.InverseTransformVectorNoScale(Torque);
    const double Omega=2*PI*FMath::Clamp(ResponseFrequencyHz,.1f,30.f),Inertia=FMath::Max(.01,Mass*FMath::Square(LeverCm*.01)/3);
    const double Time=FMath::Min(double(Dt),.05);const int32 Steps=FMath::Max(1,FMath::CeilToInt(Time*120));const double H=Time/Steps;
    for(int32 I=0;I<Steps;++I){Velocity+=(LocalForce/Mass-Position*Omega*Omega-Velocity*(2*Omega*FMath::Max(0.f,DampingRatio)))*H;Velocity=Velocity.GetClampedToMaxSize(FMath::Max(0.f,MaximumLinearSpeedCmPerSecond));Position+=Velocity*H;
        AngularVelocity+=(LocalTorque/Inertia-Angle*Omega*Omega-AngularVelocity*(2*Omega*FMath::Max(0.f,DampingRatio)))*H;AngularVelocity=AngularVelocity.GetClampedToMaxSize(FMath::DegreesToRadians(FMath::Max(0.f,MaximumAngularSpeedDegreesPerSecond)));Angle+=AngularVelocity*H;
        auto Bound=[](FVector& X,FVector& V,double Limit){if(X.SizeSquared()>Limit*Limit){X=X.GetClampedToMaxSize(Limit);const FVector N=X.GetSafeNormal();V-=N*FMath::Max(0.,FVector::DotProduct(V,N));}};Bound(Position,Velocity,FMath::Max(0.f,MaximumOffsetCm));Bound(Angle,AngularVelocity,FMath::DegreesToRadians(FMath::Max(0.f,MaximumRotationDegrees)));Position=Position.GetClampedToMaxSize(FMath::Max(0.f,MaximumOffsetCm));Angle=Angle.GetClampedToMaxSize(FMath::DegreesToRadians(FMath::Max(0.f,MaximumRotationDegrees)));}
    if(Position.ContainsNaN() || Angle.ContainsNaN()){ResetResponse();Status=TEXT("Body response invalid state reset");return;}
    const double R=Angle.Size();Offsets.Reset();Offsets.Add(Bone,FTransform(R>1e-8?FQuat(Angle/R,R):FQuat::Identity,Position));
    Status=FString::Printf(TEXT("Body response: %s | force %.2f N | offset %.2f cm"),UseProxy?TEXT("rigid proxy + soft bridge"):TEXT("soft contact bridge"),Force.Size(),Position.Size());
}

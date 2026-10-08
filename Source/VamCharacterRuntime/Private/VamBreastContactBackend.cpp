#include "VamBreastContactBackend.h"
#include "ChaosFlesh/FleshDynamicAsset.h"
#include "Chaos/Sphere.h"
#include "Chaos/Box.h"

UDeformablePhysicsComponent::FDataMapValue UVamBreastContactFlesh::NewDeformableData()
{
    auto Data=Super::NewDeformableData();
    if(Data) if(auto* Input=Data->As<Chaos::Softs::FFleshThreadingProxy::FFleshInputBuffer>())
    {
        Input->Transforms=InputPose;Input->RestTransforms=ReferencePose;
    }
    return Data;
}

void UVamBreastContactFlesh::UpdateFromSimulation(const FDataMapValue* Buffer)
{
    Super::UpdateFromSimulation(Buffer);
    OutputPositions.Reset();
    if(const auto* Dynamic=GetDynamicCollection()) if(const auto* Positions=Dynamic->FindPositions())
        for(const auto& P:*Positions) OutputPositions.Add(FVector(P));
}

UDeformablePhysicsComponent::FThreadingProxy* UVamBreastContactCollisions::NewProxy()
{
    Previous.Reset();Keys.Reset();RetiredKeys.Reset();
    return new Chaos::Softs::FCollisionManagerProxy(this);
}

UDeformablePhysicsComponent::FDataMapValue UVamBreastContactCollisions::NewDeformableData()
{
    using namespace Chaos::Softs;
    TArray<FCollisionObjectAddedBodies> Added;
    TArray<FCollisionObjectRemovedBodies> Removed;
    TArray<FCollisionObjectUpdatedBodies> Updated;
    RetiredKeys=Keys;
    TArray<TObjectPtr<UObject>> NextKeys;NextKeys.SetNum(Spheres.Num());
    for(int32 I=0;I<Spheres.Num();++I)
    {
        const auto& S=Spheres[I];
        if(S.WorldCenter.ContainsNaN() || !FMath::IsFinite(S.RadiusCm) || S.RadiusCm<=0)
        {
            if(Keys.IsValidIndex(I) && Keys[I]) Removed.Add({{Keys[I],ERigidCollisionShapeType::Sphere,0}});
            continue;
        }
        UObject* Key=Keys.IsValidIndex(I)?Keys[I].Get():nullptr;
        if(Key && Previous.IsValidIndex(I) && FMath::IsNearlyEqual(Previous[I].RadiusCm,S.RadiusCm) && Previous[I].bPlaten==S.bPlaten && Previous[I].HalfExtentCm.Equals(S.HalfExtentCm))
            Updated.Add({{Key,ERigidCollisionShapeType::Sphere,0},FTransform(S.WorldRotation,S.WorldCenter)});
        else
        {
            if(Key) Removed.Add({{Key,ERigidCollisionShapeType::Sphere,0}});
            Key=NewObject<UVamBreastContactSource>(this,NAME_None,RF_Transient);
            Added.Add({{Key,ERigidCollisionShapeType::Sphere,0},FTransform(S.WorldRotation,S.WorldCenter),TEXT("VamBreastContact"),S.bPlaten?static_cast<Chaos::FImplicitObject*>(new Chaos::TBox<Chaos::FReal,3>(-S.HalfExtentCm,S.HalfExtentCm)):static_cast<Chaos::FImplicitObject*>(new Chaos::FSphere(Chaos::FVec3(0),S.RadiusCm))});
        }
        NextKeys[I]=Key;
    }
    for(int32 I=Spheres.Num();I<Keys.Num();++I) if(Keys[I]) Removed.Add({{Keys[I],ERigidCollisionShapeType::Sphere,0}});
    Previous=Spheres;Keys=MoveTemp(NextKeys);
    return FDataMapValue(new FCollisionManagerProxy::FCollisionsInputBuffer(Added,Removed,Updated,this));
}

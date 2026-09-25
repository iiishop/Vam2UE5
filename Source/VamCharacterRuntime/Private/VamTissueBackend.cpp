#include "VamTissueBackend.h"
#include "Components/SkeletalMeshComponent.h"
#include "Chaos/Sphere.h"
#include "Chaos/Box.h"
#include "Chaos/Capsule.h"
#include "Chaos/Convex.h"

UDeformablePhysicsComponent::FDataMapValue UVamTissueFleshComponent::NewDeformableData()
{
    auto Data=Super::NewDeformableData();
    if(InputBody.IsValid() && Data)
        if(auto* Input=Data->As<Chaos::Softs::FFleshThreadingProxy::FFleshInputBuffer>())
        {
            Input->Transforms=InputBody->GetComponentSpaceTransforms();
            Input->RestTransforms=ReferenceCS;
        }
    return Data;
}
void UVamTissueFleshComponent::UpdateFromSimulation(const FDataMapValue* Buffer)
{
    Super::UpdateFromSimulation(Buffer);
    if(Buffer && *Buffer && (*Buffer)->As<Chaos::Softs::FFleshThreadingProxy::FFleshOutputBuffer>()) ++CompletedOutputs;
}
UDeformablePhysicsComponent::FThreadingProxy* UVamTissueCollisionComponent::NewProxy()
{
    Previous.Reset();Keys.Reset();KeyOwners.Reset();RetiredKeys.Reset();return new Chaos::Softs::FCollisionManagerProxy(this);
}
UDeformablePhysicsComponent::FDataMapValue UVamTissueCollisionComponent::NewDeformableData()
{
    using namespace Chaos;using namespace Chaos::Softs;
    TArray<FCollisionObjectAddedBodies> Added;TArray<FCollisionObjectRemovedBodies> Removed;TArray<FCollisionObjectUpdatedBodies> Updated;
    TMap<FCollisionObjectKey,FVamTissueCollider> Next;
    TMap<FCollisionObjectKey,UObject*> NextKeys;RetiredKeys=KeyOwners;KeyOwners.Reset();
    for(const auto& Shape:Shapes)
    {
        const auto* Old=Previous.Find(Shape.Key);
        UObject* Token=Old?Keys.FindRef(Shape.Key):nullptr;
        if(Old && Old->Size.Equals(Shape.Size) && Old->Convex==Shape.Convex)
            Updated.Add({{Token,Shape.Key.Get<1>(),0},Shape.Transform});
        else
        {
            // UE removes all shapes sharing a source key. Own a key per geometry and
            // replace it on size changes so an old removal cannot remove its replacement.
            if(Old) Removed.Add({{Token,Shape.Key.Get<1>(),0}});
            Token=NewObject<UVamTissueCollisionKey>(this,NAME_None,RF_Transient);
            FImplicitObject* Geometry=nullptr;
            switch(Shape.Key.Get<1>())
            {
            case ERigidCollisionShapeType::Sphere: Geometry=new Chaos::FSphere(FVec3(0),Shape.Size.X);break;
            case ERigidCollisionShapeType::Box: Geometry=new TBox<Chaos::FReal,3>(-Shape.Size,Shape.Size);break;
            case ERigidCollisionShapeType::Sphyl: Geometry=new FCapsule(FVec3(0,0,-Shape.Size.Z),FVec3(0,0,Shape.Size.Z),Shape.Size.X);break;
            case ERigidCollisionShapeType::Convex: Geometry=new FConvex(Shape.Convex,0.f);break;
            default: break;
            }
            if(Geometry) Added.Add({{Token,Shape.Key.Get<1>(),0},Shape.Transform,TEXT("VamRuntimeCollider"),Geometry});
        }
        Next.Add(Shape.Key,Shape);
        NextKeys.Add(Shape.Key,Token);KeyOwners.Add(Token);
    }
    for(const auto& Old:Previous) if(!Next.Contains(Old.Key)) Removed.Add({{Keys.FindRef(Old.Key),Old.Key.Get<1>(),0}});
    Previous=MoveTemp(Next);Keys=MoveTemp(NextKeys);
    return FDataMapValue(new FCollisionManagerProxy::FCollisionsInputBuffer(Added,Removed,Updated,this));
}

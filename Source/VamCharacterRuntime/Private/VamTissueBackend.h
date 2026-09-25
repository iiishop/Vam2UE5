#pragma once
#include "CoreMinimal.h"
#include "ChaosFlesh/FleshComponent.h"
#include "ChaosFlesh/ChaosDeformablePhysicsComponent.h"
#include "Chaos/Deformable/ChaosDeformableCollisionsProxy.h"
#include "Chaos/Convex.h"
#include "VamTissueBackend.generated.h"

UCLASS()
class UVamTissueCollisionKey : public UObject
{
    GENERATED_BODY()
};

UCLASS()
class UVamTissueFleshComponent : public UFleshComponent
{
    GENERATED_BODY()
public:
    TWeakObjectPtr<class USkeletalMeshComponent> InputBody;
    TArray<FTransform> ReferenceCS;
    int64 CompletedOutputs=0;
    virtual FDataMapValue NewDeformableData() override;
    virtual void UpdateFromSimulation(const FDataMapValue* Buffer) override;
};

struct FVamTissueCollider
{
    Chaos::Softs::FCollisionObjectKey Key;
    FTransform Transform;
    FVector Size;
    TArray<Chaos::FConvex::FVec3Type> Convex;
};

/** Runtime adapter for ordinary primitive collision, including capsules omitted by UE's static-mesh adapter. */
UCLASS()
class UVamTissueCollisionComponent : public UDeformablePhysicsComponent
{
    GENERATED_BODY()
public:
    TArray<FVamTissueCollider> Shapes;
    TMap<Chaos::Softs::FCollisionObjectKey,FVamTissueCollider> Previous;
    TMap<Chaos::Softs::FCollisionObjectKey,UObject*> Keys;
    UPROPERTY(Transient) TArray<TObjectPtr<UObject>> KeyOwners;
    UPROPERTY(Transient) TArray<TObjectPtr<UObject>> RetiredKeys;
    virtual FThreadingProxy* NewProxy() override;
    virtual FDataMapValue NewDeformableData() override;
};

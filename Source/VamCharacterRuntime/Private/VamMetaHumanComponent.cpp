#include "VamMetaHumanComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Actor.h"

void UVamMetaHumanComponent::BeginPlay()
{
    Super::BeginPlay(); InstanceId=FGuid::NewGuid(); Ready=false; LastError.Reset();
    TArray<USkeletalMeshComponent*> Meshes; GetOwner()->GetComponents(Meshes);
    bool Face=false, Body=false;
    for (const auto* Mesh:Meshes)
    {
        if (!Mesh->GetSkeletalMeshAsset()) continue;
        Face |= Mesh->GetFName()==TEXT("Face"); Body |= Mesh->GetFName()==TEXT("Body");
    }
    Ready=Face && Body;
    if (!Ready) LastError=TEXT("Assembly Face/Body components unavailable; no native fallback applied");
}
void UVamMetaHumanComponent::EndPlay(const EEndPlayReason::Type Reason)
{ Ready=false; Super::EndPlay(Reason); }

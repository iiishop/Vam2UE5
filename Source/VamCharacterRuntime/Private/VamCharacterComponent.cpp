#include "VamCharacterComponent.h"
#include "VamCharacterDefinition.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/Skeleton.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"

void UVamCharacterComponent::BeginPlay() { Super::BeginPlay(); LoadCharacter(); }
void UVamCharacterComponent::EndPlay(const EEndPlayReason::Type Reason) { UnloadCharacter(); Super::EndPlay(Reason); }

void UVamCharacterComponent::UnloadCharacter()
{
    ++Generation;
    if (Pending) { Pending->CancelHandle(); Pending.Reset(); }
    for (auto Part : LoadedParts) if (Part) Part->DestroyComponent();
    LoadedParts.Reset();
    if (Body) Body->DestroyComponent();
    Body = nullptr;
    LoadedDefinition = nullptr;
}

void UVamCharacterComponent::LoadCharacter()
{
    UnloadCharacter();
    if (Definition.IsNull()) { OnLoaded.Broadcast(false, TEXT("No CharacterDefinition assigned")); return; }
    const uint64 Ticket = Generation;
    Pending = UAssetManager::GetStreamableManager().RequestAsyncLoad(Definition.ToSoftObjectPath(),
        FStreamableDelegate::CreateWeakLambda(this, [this, Ticket]() { LoadMeshes(Ticket); }));
}

void UVamCharacterComponent::LoadMeshes(uint64 Ticket)
{
    if (Ticket != Generation) return;
    LoadedDefinition = Definition.Get();
    if (!LoadedDefinition || !LoadedDefinition->bBuildVerified ||
        LoadedDefinition->ShapeConvention != TEXT("neutral_plus_parameters") || LoadedDefinition->Body.IsNull())
    { OnLoaded.Broadcast(false, TEXT("Definition missing or source build not verified")); return; }
    TArray<FSoftObjectPath> Paths { LoadedDefinition->Body.ToSoftObjectPath(), LoadedDefinition->Skeleton.ToSoftObjectPath() };
    for (const auto& Part : LoadedDefinition->Parts) if (!Part.IsNull()) Paths.AddUnique(Part.ToSoftObjectPath());
    Pending = UAssetManager::GetStreamableManager().RequestAsyncLoad(Paths,
        FStreamableDelegate::CreateWeakLambda(this, [this, Ticket]() { Assemble(Ticket); }));
}

void UVamCharacterComponent::Assemble(uint64 Ticket)
{
    if (Ticket != Generation || !LoadedDefinition) return;
    USkeletalMesh* Mesh = LoadedDefinition->Body.Get();
    if (!Mesh || !LoadedDefinition->Skeleton.Get() || Mesh->GetSkeleton() != LoadedDefinition->Skeleton.Get())
    { OnLoaded.Broadcast(false, TEXT("Body or exact skeleton dependency unavailable")); return; }
    Body = NewObject<USkeletalMeshComponent>(GetOwner(), NAME_None, RF_Transient);
    Body->SetupAttachment(this);
    Body->SetSkeletalMeshAsset(Mesh);
    Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Body->RegisterComponent();
    FString Missing;
    for (const auto& Reference : LoadedDefinition->Parts)
    {
        auto* PartMesh = Reference.Get();
        // Sharing is admitted only by the builder's exact bind signature and skeleton identity.
        bool Compatible = PartMesh && PartMesh->GetSkeleton() == Mesh->GetSkeleton();
        if (Compatible)
        {
            const auto& A = Mesh->GetRefSkeleton(); const auto& B = PartMesh->GetRefSkeleton();
            Compatible = A.GetNum() == B.GetNum();
            for (int32 Index=0; Compatible && Index<A.GetNum(); ++Index)
                Compatible = A.GetBoneName(Index)==B.GetBoneName(Index) && A.GetParentIndex(Index)==B.GetParentIndex(Index) &&
                    A.GetRefBonePose()[Index].Equals(B.GetRefBonePose()[Index], 1.e-6);
        }
        if (!Compatible) { Missing += Reference.ToString() + TEXT("; "); continue; }
        auto* Part = NewObject<USkeletalMeshComponent>(GetOwner(), NAME_None, RF_Transient);
        Part->SetupAttachment(Body);
        Part->SetSkeletalMeshAsset(PartMesh);
        Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Part->SetLeaderPoseComponent(Body);
        Part->RegisterComponent();
        LoadedParts.Add(Part);
    }
    for (const auto& Parameter : LoadedDefinition->Parameters) SetParameter(Parameter.Target, Parameter.DefaultValue);
    OnLoaded.Broadcast(true, Missing.IsEmpty() ? TEXT("Native character loaded") : TEXT("Body loaded; missing or incompatible parts: ") + Missing);
}

bool UVamCharacterComponent::SetParameter(FName Name, float Value)
{
    if (!Body || !LoadedDefinition || !FMath::IsFinite(Value)) return false;
    const auto* Parameter = LoadedDefinition->Parameters.FindByPredicate([Name](const FVamMorphParameter& P) { return P.Target == Name; });
    if (!Parameter || !Body->GetSkeletalMeshAsset()->FindMorphTarget(Name)) return false;
    Value = FMath::Clamp(Value, Parameter->Minimum, Parameter->Maximum);
    Body->SetMorphTarget(Name, Value);
    for (auto Part : LoadedParts) if (Part->GetSkeletalMeshAsset()->FindMorphTarget(Name)) Part->SetMorphTarget(Name, Value);
    return true;
}

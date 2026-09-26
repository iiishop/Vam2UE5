// Development-only caller. The assembled BP never depends on this test or a test level.
#include "VamMetaHumanComponent.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "Engine/World.h"
#include "Engine/SkeletalMesh.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Actor.h"
#include "TimerManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"

#if !UE_BUILD_SHIPPING
namespace
{
void FinishMHCheck(bool Passed, const FString& Reason, const FString& ClassPath)
{
    auto Report=MakeShared<FJsonObject>();
    Report->SetBoolField(TEXT("passed"),Passed);
    Report->SetBoolField(TEXT("cooked"),FPlatformProperties::RequiresCookedData());
    Report->SetStringField(TEXT("reason"),Reason);
    Report->SetStringField(TEXT("class"),ClassPath);
    Report->SetBoolField(TEXT("visual_acceptance_passed"),false);
    FString Json; FJsonSerializer::Serialize(Report,TJsonWriterFactory<>::Create(&Json));
    const bool Saved=FFileHelper::SaveStringToFile(Json,*(FPaths::ProjectSavedDir()/TEXT("MetaHumanLifecycle.json")));
    UE_LOG(LogTemp,Display,TEXT("MH00 lifecycle: %s"),*Json);
    FPlatformMisc::RequestExitWithStatus(false,Passed && Saved ? 0 : 1);
}

bool HasAnimation(AActor* Actor)
{
    bool Face=false,Body=false;
    TArray<USkeletalMeshComponent*> Meshes; Actor->GetComponents(Meshes);
    for (auto* Mesh:Meshes)
    {
        if (Mesh->GetFName()!=TEXT("Face") && Mesh->GetFName()!=TEXT("Body")) continue;
        if (!Mesh->GetSkeletalMeshAsset() || !Mesh->GetSkeletalMeshAsset()->GetSkeleton() ||
            Mesh->GetSkeletalMeshAsset()->GetLODNum()<2) return false;
        if (Mesh->GetFName()==TEXT("Face")) Face=Mesh->GetAnimInstance() && Mesh->GetPostProcessInstance();
        else Body=Mesh->GetPostProcessInstance()!=nullptr;
    }
    return Face && Body;
}

FAutoConsoleCommandWithWorldAndArgs MHCheck(TEXT("vam.MetaHuman.Verify"),
    TEXT("Development test: dynamically spawn two official MH BPs; pass generated class path."),
    FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args,UWorld* World)
{
    if (!World || !World->IsGameWorld() || Args.Num()!=1) return;
    const FString ClassPath=Args[0];
    UClass* Class=LoadClass<AActor>(nullptr,*ClassPath);
    if (!Class) { FinishMHCheck(false,TEXT("Cannot load assembled actor class"),ClassPath); return; }
    FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    AActor* A=World->SpawnActor<AActor>(Class,FVector::ZeroVector,FRotator::ZeroRotator,Params);
    AActor* B=World->SpawnActor<AActor>(Class,FVector(0,200,0),FRotator::ZeroRotator,Params);
    if (!A || !B) { FinishMHCheck(false,TEXT("Spawn failed"),ClassPath); return; }
    TWeakObjectPtr<AActor> WA=A,WB=B;
    FTimerHandle Handle;
    World->GetTimerManager().SetTimer(Handle,[WA,WB,ClassPath]()
    {
        AActor* A=WA.Get(); AActor* B=WB.Get();
        if (!A || !B) { FinishMHCheck(false,TEXT("Actor lost before tick"),ClassPath); return; }
        auto* CA=A->FindComponentByClass<UVamMetaHumanComponent>();
        auto* CB=B->FindComponentByClass<UVamMetaHumanComponent>();
        if (!CA || !CB || !CA->Ready || !CB->Ready || !CA->InstanceId.IsValid() || CA->InstanceId==CB->InstanceId ||
            !HasAnimation(A) || !HasAnimation(B))
        { FinishMHCheck(false,TEXT("BeginPlay, instance identity or official animation unavailable"),ClassPath); return; }
        TArray<USkeletalMeshComponent*> Meshes; A->GetComponents(Meshes);
        TArray<UAnimInstance*> Before,BeforePost;
        for (auto* Mesh:Meshes) { Before.Add(Mesh->GetAnimInstance()); BeforePost.Add(Mesh->GetPostProcessInstance()); }
        CA->NotifySurfaceChanged();
        bool Valid=CA->SurfaceRevision==1 && CA->ShapeRevision==0 && CA->EquipmentRevision==0 && CB->SurfaceRevision==0;
        for (int32 I=0;I<Meshes.Num();++I) Valid &= Before[I]==Meshes[I]->GetAnimInstance() && BeforePost[I]==Meshes[I]->GetPostProcessInstance();
        UWorld* W=A->GetWorld(); UClass* C=A->GetClass();
        A->Destroy(); Valid &= !CA->Ready && CB->Ready;
        FActorSpawnParameters P; P.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        AActor* Replacement=W->SpawnActor<AActor>(C,FVector::ZeroVector,FRotator::ZeroRotator,P);
        auto* CR=Replacement ? Replacement->FindComponentByClass<UVamMetaHumanComponent>() : nullptr;
        Valid &= CR && CR->Ready && CR->InstanceId!=CB->InstanceId && CR->SurfaceRevision==0;
        if (Replacement) Replacement->Destroy(); B->Destroy();
        FinishMHCheck(Valid,Valid?TEXT("Dynamic spawn, BeginPlay, animation, surface isolation, EndPlay and respawn passed"):
            TEXT("Revision isolation or destroy/respawn failed"),ClassPath);
    },1.0f,false);
}));
}
#endif

#include "VamSoftTissueRuntimeProbe.h"
#include "VamCharacterActor.h"
#include "VamCharacterComponent.h"
#include "VamCharacterDefinition.h"
#include "VamSoftTissueComponent.h"
#include "VamMotionComponent.h"
#include "VamShapeAnimInstance.h"
#include "ChaosFlesh/ChaosDeformableSolverComponent.h"
#include "ChaosFlesh/FleshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Engine/World.h"
#include "Engine/Engine.h"

AVamSoftTissueRuntimeProbe::AVamSoftTissueRuntimeProbe() {PrimaryActorTick.bCanEverTick=true;PrimaryActorTick.TickGroup=TG_PostUpdateWork;}
void AVamSoftTissueRuntimeProbe::Finish(bool Passed,const FString& Reason)
{
    TSharedRef<FJsonObject> R=MakeShared<FJsonObject>();R->SetBoolField(TEXT("architecture_passed"),Passed);R->SetBoolField(TEXT("visual_acceptance_passed"),false);
    R->SetStringField(TEXT("reason"),Reason);R->SetNumberField(TEXT("spawn_destroy_cycles"),Cycles);R->SetNumberField(TEXT("phase"),Phase);
    TArray<TSharedPtr<FJsonValue>> Rows;
    for(const auto& A:Subjects) if(A && A->SoftTissue)
    {const auto O=A->SoftTissue->GetBodySurfaceOutput();auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("state"),StaticEnum<EVamSoftTissueState>()->GetNameStringByValue(int64(A->SoftTissue->State)));Row->SetStringField(TEXT("error"),A->SoftTissue->LastError);Row->SetStringField(TEXT("instance"),O.Instance.ToString());Row->SetNumberField(TEXT("solver_revision"),O.SolverRevision);Row->SetNumberField(TEXT("collision_vertices"),O.CollisionVertices.Num());Rows.Add(MakeShared<FJsonValueObject>(Row));}
    R->SetArrayField(TEXT("subjects"),Rows);FString Json;FJsonSerializer::Serialize(R,TJsonWriterFactory<>::Create(&Json));FFileHelper::SaveStringToFile(Json,*(FPaths::ProjectSavedDir()/TEXT("SoftTissueArchitecture.json")));
    SetActorTickEnabled(false);FPlatformMisc::RequestExit(false);
}
void AVamSoftTissueRuntimeProbe::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);if(!FParse::Param(FCommandLine::Get(),TEXT("VamSoftTissueAudit"))) return;
    const double Now=GetWorld()->GetTimeSeconds();if(Started==0) Started=Now;
    if(Now-Started>90) {Finish(false,TEXT("runtime_lifecycle_timeout"));return;}
    if(Subjects.Num()!=3) {Finish(false,TEXT("three_regular_characters_required"));return;}
    for(const auto& S:Subjects) if(!S || !S->Character->Body) return;
    auto* A=Subjects[0].Get();auto* B=Subjects[1].Get();auto* T=A->SoftTissue.Get();auto* Peer=B->SoftTissue.Get();
    if(T->State==EVamSoftTissueState::Error || Peer->State==EVamSoftTissueState::Error) {Finish(false,TEXT("runtime_error"));return;}
    if(Spawned && Spawned->SoftTissue->State==EVamSoftTissueState::Error) {Finish(false,TEXT("spawned_runtime_error: ")+Spawned->SoftTissue->LastError);return;}
    if(Phase==0)
    {
        if(!T->GetBodySurfaceOutput().Valid || !Peer->GetBodySurfaceOutput().Valid) return;
        if(Subjects[2]->SoftTissue->State!=EVamSoftTissueState::Disabled || T->GetBodySurfaceOutput().Instance==Peer->GetBodySurfaceOutput().Instance || T->GetBodySurfaceOutput().SurfaceResource==Peer->GetBodySurfaceOutput().SurfaceResource)
        {Finish(false,TEXT("optional_profile_or_instance_isolation_failed"));return;}
        PeerShape=Peer->GetBodySurfaceOutput().ShapeRevision;PeerIdentity=Peer->GetBodySurfaceOutput().Instance;
        const auto* Animation=Cast<UVamShapeAnimInstance>(A->Character->Body->GetAnimInstance());
        if(!Animation || !Animation->GetBaseAnimation()) {Finish(false,TEXT("base_animation_missing"));return;}
        AnimationBeforeOff=Animation->GetBaseAnimationTime();
        T->SetSoftTissueEnabled(false);
        if(T->GetBodySurfaceOutput().Valid || !A->Character->Body->IsVisible()) {Finish(false,TEXT("disable_did_not_restore_native_surface"));return;}
        Phase=1;PhaseTime=Now;
    }
    else if(Phase==1 && Now-PhaseTime>.5)
    {
        const auto* Animation=Cast<UVamShapeAnimInstance>(A->Character->Body->GetAnimInstance());
        if(!Animation || FMath::IsNearlyEqual(AnimationBeforeOff,Animation->GetBaseAnimationTime(),1.e-4)) {Finish(false,TEXT("off_stopped_animation"));return;}
        T->SetSoftTissueQuality(EVamSoftTissueQuality::High);Phase=2;
    }
    else if(Phase==2 && T->GetBodySurfaceOutput().Valid)
    {
        const auto* Definition=A->Character->Definition.Get();
        const auto* P=Definition->Parameters.FindByPredicate([](const auto& Item){return Item.Group==TEXT("Shape") && Item.Maximum>Item.Minimum;});
        if(!P) {Finish(false,TEXT("shape_parameter_missing"));return;}
        Parameter=P->Target;Value=FMath::Lerp(P->Minimum,P->Maximum,.52f);
        if(!A->Character->PreviewParameters({{Parameter,Value}}) || T->GetBodySurfaceOutput().Valid) {Finish(false,TEXT("shape_invalidation_failed"));return;}
        Phase=3;
    }
    else if(Phase==3 && T->GetBodySurfaceOutput().Valid) {A->Character->CancelShape();Phase=4;}
    else if(Phase==4 && T->GetBodySurfaceOutput().Valid)
    {A->Character->SetParameter(Parameter,Value);A->Character->CommitShape();Phase=5;}
    else if(Phase==5 && T->GetBodySurfaceOutput().Valid)
    {FTransform X=A->GetActorTransform();X.AddToTranslation(FVector(20,0,0));A->Motion->TeleportTo(X,Now);if(T->GetBodySurfaceOutput().Valid){Finish(false,TEXT("teleport_output_stale"));return;}Phase=6;}
    else if(Phase==6 && T->GetBodySurfaceOutput().Valid)
    {
        if(Peer->GetBodySurfaceOutput().ShapeRevision!=PeerShape || Peer->GetBodySurfaceOutput().Instance!=PeerIdentity) {Finish(false,TEXT("peer_mutated"));return;}
        FActorSpawnParameters Spawn;Spawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        Spawned=GetWorld()->SpawnActor<AVamCharacterActor>(A->GetClass(),FVector(0,450,0),FRotator::ZeroRotator,Spawn);Phase=7;
    }
    else if(Phase==7 && Spawned && Spawned->SoftTissue->GetBodySurfaceOutput().Valid)
    {
        TArray<UActorComponent*> Components;Spawned->GetComponents(Components);for(auto* C:Components) DestroyedComponents.Add(C);
        Spawned->Destroy();Spawned=nullptr;++Cycles;PhaseTime=Now;Phase=8;
        GEngine->ForceGarbageCollection(true);
    }
    else if(Phase==8 && Now-PhaseTime>.2)
    {
        for(const auto& Component:DestroyedComponents) if(Component.IsValid() && Component->IsRegistered()) {Finish(false,TEXT("destroy_left_registered_component"));return;}
        for(const auto& Component:DestroyedComponents) if(Component.IsValid()) {Finish(false,TEXT("destroy_left_component_after_gc"));return;}
        DestroyedComponents.Reset();if(Cycles<3) {Phase=6;return;}
        for(int32 I=0;I<2;++I)
        {
            TArray<UDeformableSolverComponent*> Solvers;Subjects[I]->GetComponents(Solvers);
            if(Solvers.Num()!=1) {Finish(false,TEXT("private_solver_count_failed"));return;}
            auto* Anim=Cast<UVamShapeAnimInstance>(Subjects[I]->Character->Body->GetAnimInstance());
            if(!Anim || !Subjects[I]->SoftTissue->GetBodySurfaceOutput().Valid) {Finish(false,TEXT("final_runtime_not_ready"));return;}
        }
        Finish(true,TEXT("regular_character_load_off_on_shape_teleport_spawn_destroy"));
    }
}

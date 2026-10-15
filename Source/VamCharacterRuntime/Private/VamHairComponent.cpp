#include "VamHairComponent.h"
#include "VamCharacterActor.h"
#include "VamCharacterComponent.h"
#include "VamRuntimeConfiguration.h"
#include "Components/SkeletalMeshComponent.h"
#include "GroomComponent.h"
#include "GroomAsset.h"
#include "VamHairAsset.h"
#include "PhysicsEngine/PhysicsAsset.h"

UVamHairComponent::UVamHairComponent()
{
    PrimaryComponentTick.bCanEverTick=true;
    PrimaryComponentTick.TickGroup=TG_PostPhysics;
    bTickInEditor=true;
}
void UVamHairComponent::ReleaseGroom(){if(Groom)Groom->DestroyComponent();Groom=nullptr;BoundBody.Reset();AppliedProfile=nullptr;}
void UVamHairComponent::EndPlay(const EEndPlayReason::Type Reason){ReleaseGroom();Super::EndPlay(Reason);}
void UVamHairComponent::SetHairProfile(UVamHairProfile* P){Profile=P;ReleaseGroom();}
void UVamHairComponent::SetHairQuality(EVamHairQuality Q){Quality=Q;}
void UVamHairComponent::ResetHairSimulation(){if(Groom)Groom->ResetSimulation();}
void UVamHairComponent::SetHairEnabled(bool InEnabled){bEnabled=InEnabled;if(!bEnabled)ReleaseGroom();}
void UVamHairComponent::SetHairSimulationEnabled(bool InEnabled){bSimulate=InEnabled;if(Groom){Groom->SimulationSettings.bOverrideSettings=!bSimulate;Groom->SimulationSettings.SolverSettings.bEnableSimulation=bSimulate;Groom->UpdateHairSimulation();Groom->ResetSimulation();}}
void UVamHairComponent::TickComponent(float Dt,ELevelTick Type,FActorComponentTickFunction* Tick)
{
    Super::TickComponent(Dt,Type,Tick);
    if(!bEnabled){if(Groom)ReleaseGroom();Status=TEXT("Hair disabled");return;}
    auto* A=Cast<AVamCharacterActor>(GetOwner());auto* C=A?A->Character.Get():nullptr;
    auto* B=C?C->Body.Get():nullptr;
    auto Selected=Profile;
    if(Selected.IsNull()&&C&&C->GetLoadedRuntimeConfiguration())Selected=C->GetLoadedRuntimeConfiguration()->Hair;
    auto* P=Selected.LoadSynchronous();
    if(!B||!P){if(Groom)ReleaseGroom();Status=TEXT("No loaded body or hair profile");return;}
    if(const auto* Config=C->GetLoadedRuntimeConfiguration();Config&&!Config->Hairstyle.IsNull())
    {
        auto* Style=Config->Hairstyle.LoadSynchronous();
        if(!Style||P->Hairstyle!=Config->Hairstyle||P->HairstyleSignature!=Style->GetContentSignature())
        {if(Groom)ReleaseGroom();Status=TEXT("Hair rejected: hairstyle composition changed; rebuild its body-fit cache");return;}
    }
    if(!P->ReferenceBody.IsNull()&&P->ReferenceBody.ToSoftObjectPath()!=FSoftObjectPath(B->GetSkeletalMeshAsset())){if(Groom)ReleaseGroom();Status=TEXT("Hair rejected: refit/rebuild for this body mesh");return;}
    if(Groom&&BoundBody==B&&AppliedProfile==P&&AppliedQuality==Quality)return;
    ReleaseGroom();
    const int32 Head=B->GetBoneIndex(P->AttachmentBone);
    auto* Asset=(Quality==EVamHairQuality::Fine?P->FineGroom:P->CoarseGroom).LoadSynchronous();
    if(Head==INDEX_NONE||!Asset){Status=TEXT("Hair rejected: attachment bone or quality asset missing");return;}
    Groom=NewObject<UGroomComponent>(GetOwner(),NAME_None,RF_Transient);
    Groom->SetupAttachment(B,P->AttachmentBone);
    Groom->AttachmentName=P->AttachmentBone.ToString();
    Groom->SetGroomAsset(Asset);
    Groom->SimulationSettings.bOverrideSettings=!bSimulate;
    Groom->SimulationSettings.SolverSettings.bEnableSimulation=bSimulate;
    // Native local-space drag only sees velocity relative to the simulation bone.
    // World-space nodes include translation and rotation exactly once in air drag;
    // native root binding still follows the animated head.
    Groom->SimulationSettings.SimulationSetup.bLocalSimulation=false;
    Groom->SimulationSettings.SimulationSetup.LocalBone=P->AttachmentBone.ToString();
    Groom->SetPhysicsAsset(P->HairCollision.IsNull()?B->GetPhysicsAsset():P->HairCollision.LoadSynchronous());
    for(int32 I=0;I<P->Materials.Num();++I)Groom->SetMaterial(I,P->Materials[I].LoadSynchronous());
    Groom->RegisterComponent();Groom->AddTickPrerequisiteComponent(B);Groom->SetForcedLOD(0);
    Groom->ResetSimulation();BoundBody=B;AppliedProfile=P;AppliedQuality=Quality;
    Status=FString::Printf(TEXT("UE Groom / physics %s / %s / %d groups / %s"),bSimulate?TEXT("on"):TEXT("off"),Quality==EVamHairQuality::Fine?TEXT("Fine"):TEXT("Coarse"),Asset->GetNumHairGroups(),*P->BuildIdentity);
    UE_LOG(LogTemp,Display,TEXT("VAM_HAIR %s"),*Status);
}


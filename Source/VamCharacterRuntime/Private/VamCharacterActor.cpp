#include "VamCharacterActor.h"
#include "VamCharacterComponent.h"
#include "VamMotionComponent.h"
#include "VamInteractionComponent.h"
#include "PhysicsEngine/PhysicsHandleComponent.h"
#include "PhysicsEngine/PhysicalAnimationComponent.h"
#include "VamActivePoseComponent.h"
#include "VamPhysicsOutputComponent.h"
#include "VamBreastContactComponent.h"
AVamCharacterActor::AVamCharacterActor()
{
    Character = CreateDefaultSubobject<UVamCharacterComponent>(TEXT("Character"));
    SetRootComponent(Character);
    Motion = CreateDefaultSubobject<UVamMotionComponent>(TEXT("Motion"));
    Interaction = CreateDefaultSubobject<UVamInteractionComponent>(TEXT("Interaction"));
    PhysicsHandle = CreateDefaultSubobject<UPhysicsHandleComponent>(TEXT("PhysicsHandle"));
    PhysicalAnimation = CreateDefaultSubobject<UPhysicalAnimationComponent>(TEXT("PhysicalAnimation"));
    ActivePose = CreateDefaultSubobject<UVamActivePoseComponent>(TEXT("ActivePose"));
    PhysicsOutput = CreateDefaultSubobject<UVamPhysicsOutputComponent>(TEXT("PhysicsOutput"));
    BreastContact = CreateDefaultSubobject<UVamBreastContactComponent>(TEXT("BreastContact"));
}
void AVamCharacterActor::LoadCharacter() { Character->LoadCharacter(); }

void AVamCharacterActor::SetBreastContactEnabled(bool bEnabled) { if(BreastContact) BreastContact->SetContactEnabled(bEnabled); }
bool AVamCharacterActor::IsBreastContactEnabled() const { return BreastContact && BreastContact->IsContactEnabled(); }

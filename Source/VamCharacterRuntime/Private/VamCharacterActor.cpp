#include "VamCharacterActor.h"
#include "VamCharacterComponent.h"
AVamCharacterActor::AVamCharacterActor()
{
    Character = CreateDefaultSubobject<UVamCharacterComponent>(TEXT("Character"));
    SetRootComponent(Character);
}
void AVamCharacterActor::LoadCharacter() { Character->LoadCharacter(); }

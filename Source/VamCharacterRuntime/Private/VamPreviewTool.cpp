#include "VamPreviewTool.h"
#include "VamCharacterActor.h"
#include "VamCharacterComponent.h"
#include "VamInteractionComponent.h"
#include "VamMotionComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"

AVamPreviewTool::AVamPreviewTool()
{
    PrimaryActorTick.bCanEverTick=true;
    PrimaryActorTick.TickGroup=TG_PrePhysics;
}
bool AVamPreviewTool::MouseOnPlane(APlayerController* PC, FVector& Point) const
{
    FVector Origin,Direction;
    if (!PC || !PC->DeprojectMousePositionToWorld(Origin,Direction)) return false;
    const double Denominator=FVector::DotProduct(Direction,PlaneNormal);
    if (FMath::Abs(Denominator)<0.01) return false;
    const double Distance=FVector::DotProduct(Anchor-Origin,PlaneNormal)/Denominator;
    if (Distance<=0) return false;
    Point=Origin+Direction*Distance;
    return true;
}
bool AVamPreviewTool::FindControlAtMouse(APlayerController* PC, AVamCharacterActor*& OutActor, int32& OutBone, FVector& OutPosition) const
{
    float X,Y;
    if (!PC || !PC->GetMousePosition(X,Y)) return false;
    TArray<AActor*> Actors;
    UGameplayStatics::GetAllActorsOfClass(GetWorld(),AVamCharacterActor::StaticClass(),Actors);
    float Best=HitRadiusPixels*HitRadiusPixels;
    for (AActor* Actor:Actors)
    {
        auto* Character=Cast<AVamCharacterActor>(Actor);
        if (!Character || !Character->Character || !Character->Character->Body) continue;
        USkeletalMeshComponent* Body=Character->Character->Body;
        for (int32 I=0;I<Body->GetNumBones();++I)
        {
            const FVector World=Body->GetBoneLocation(Body->GetBoneName(I));
            FVector2D Screen;
            if (!PC->ProjectWorldLocationToScreen(World,Screen)) continue;
            const float Distance=FVector2D::DistSquared(Screen,FVector2D(X,Y));
            if (Distance<Best) { Best=Distance; OutActor=Character; OutBone=I; OutPosition=World; }
        }
    }
    return OutActor!=nullptr;
}
void AVamPreviewTool::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    APlayerController* PC=GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
    if (!PC) return;
    PC->bShowMouseCursor=true;
    if (bShowBoneControls)
    {
        TArray<AActor*> Actors;
        UGameplayStatics::GetAllActorsOfClass(GetWorld(),AVamCharacterActor::StaticClass(),Actors);
        for (AActor* Actor:Actors)
        {
            auto* Character=Cast<AVamCharacterActor>(Actor);
            if (!Character || !Character->Character || !Character->Character->Body) continue;
            USkeletalMeshComponent* Body=Character->Character->Body;
            for (int32 I=0;I<Body->GetNumBones();++I)
                DrawDebugSphere(GetWorld(),Body->GetBoneLocation(Body->GetBoneName(I)),1.8f,6,FColor::Cyan,false,0.f,0,0.65f);
            if (Character->Motion)
                for (const FVamInertiaRegion& Region:Character->Motion->GetInertiaRegions())
                {
                    const FVector At=Character->GetActorTransform().TransformPosition(Region.LocalAnchor);
                    const FVector Deflected=Character->GetActorTransform().TransformPosition(Region.LocalAnchor+Region.LocalDisplacement);
                    DrawDebugLine(GetWorld(),At,Deflected,FColor::Yellow,false,0.f,0,1.f);
                    DrawDebugSphere(GetWorld(),Deflected,3.f,8,FColor::Yellow,false,0.f);
                }
        }
    }
    if (PC->WasInputKeyJustPressed(EKeys::LeftMouseButton) || PC->WasInputKeyJustPressed(EKeys::RightMouseButton))
    {
        DragActor=nullptr; DragBone=INDEX_NONE; bPhysicsGrab=false;
        AVamCharacterActor* HitActor=nullptr; int32 Bone=INDEX_NONE; FVector Position;
        const bool bFound=FindControlAtMouse(PC,HitActor,Bone,Position);
        if (bFound)
        {
            DragActor=HitActor; DragBone=Bone;
            Target=HitActor;
            bRootDrag=PC->IsInputKeyDown(EKeys::RightMouseButton);
            Anchor=Position; RootStart=DragActor->GetActorTransform();
            if (DragActor->Character->Body)
            {
                const FName Name=DragActor->Character->Body->GetBoneName(Bone);
                BoneStart=DragActor->Character->GetDebugBoneOffset(Bone);
                if (!bRootDrag && DragActor->Interaction) bPhysicsGrab=DragActor->Interaction->GrabBone(Name,Position);
            }
            FVector Origin,Direction;
            if (PC->DeprojectMousePositionToWorld(Origin,Direction)) PlaneNormal=Direction.GetSafeNormal();
        }
    }
    if (DragActor)
    {
        const bool bHeld=PC->IsInputKeyDown(bRootDrag ? EKeys::RightMouseButton : EKeys::LeftMouseButton);
        if (!bHeld)
        {
            if (bPhysicsGrab && DragActor->Interaction) DragActor->Interaction->ReleaseGrab();
            DragActor=nullptr; DragBone=INDEX_NONE; bPhysicsGrab=false;
        }
        else
        {
            FVector Position;
            if (MouseOnPlane(PC,Position))
            {
                if (bRootDrag && DragActor->Motion)
                {
                    FTransform NewRoot=RootStart;
                    NewRoot.AddToTranslation(Position-Anchor);
                    DragActor->Motion->MoveContinuously(NewRoot,GetWorld()->GetTimeSeconds());
                }
                else if (bPhysicsGrab && DragActor->Interaction) DragActor->Interaction->MoveGrab(Position);
                else if (DragActor->Character && DragActor->Character->Body && DragBone!=INDEX_NONE)
                {
                    FTransform Offset=BoneStart;
                    Offset.AddToTranslation(DragActor->Character->Body->GetComponentTransform().InverseTransformVector(Position-Anchor));
                    DragActor->Character->SetDebugBoneOffset(DragBone,Offset);
                }
            }
        }
    }
    if (!Target) return;
    if (PC->WasInputKeyJustPressed(EKeys::P) && Target->Motion) Target->Motion->SetPreviewPaused(!Target->Motion->GetClock().bPaused);
    if (PC->WasInputKeyJustPressed(EKeys::O) && Target->Motion) Target->Motion->StepPreview();
    if (PC->WasInputKeyJustPressed(EKeys::R) && Target->Motion) Target->Motion->ResetPreview();
    if (PC->WasInputKeyJustPressed(EKeys::G) && Target->Interaction) Target->Interaction->SetPhysicalMode(EVamPhysicalMode::Ragdoll);
    if (PC->WasInputKeyJustPressed(EKeys::C) && Target->Interaction) Target->Interaction->SetPhysicalMode(EVamPhysicalMode::Controlled);
}

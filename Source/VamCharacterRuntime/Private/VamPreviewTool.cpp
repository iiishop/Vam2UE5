#include "VamPreviewTool.h"
#include "VamCharacterActor.h"
#include "VamCharacterComponent.h"
#include "VamInteractionComponent.h"
#include "VamMotionComponent.h"
#include "VamRigProfile.h"
#include "Components/SkeletalMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"

namespace
{
FVector RootControlPosition(const AVamCharacterActor* Actor)
{
    const UVamCharacterComponent* Character=Actor ? Actor->Character.Get() : nullptr;
    const USkeletalMeshComponent* Body=Character ? Character->Body.Get() : nullptr;
    const UVamRigProfile* Rig=Character ? Character->RigProfile.Get() : nullptr;
    if (Body && Rig)
    {
        const FName RootBone=Rig->BoneForSemantic(TEXT("root"));
        if (!RootBone.IsNone() && Body->GetBoneIndex(RootBone)!=INDEX_NONE)
            return Body->GetBoneLocation(RootBone)+Actor->GetActorRightVector()*8.f;
    }
    return Actor ? Actor->GetActorLocation()+FVector(0,0,100) : FVector::ZeroVector;
}
void EnableRootPhysicalResponse(AVamCharacterActor* Actor)
{
    if (!Actor || !Actor->Interaction || Actor->Interaction->Mode!=EVamPhysicalMode::Controlled || !Actor->Character) return;
    const UVamRigProfile* Rig=Actor->Character->RigProfile.Get();
    const FName Pelvis=Rig ? Rig->BoneForSemantic(TEXT("pelvis")) : NAME_None;
    const FName Spine=Rig ? Rig->BoneForSemantic(TEXT("spine")) : NAME_None;
    if (!Pelvis.IsNone() && !Spine.IsNone()) Actor->Interaction->SetRootMotionResponse(Pelvis,Spine);
}
}

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
bool AVamPreviewTool::FindControlAtMouse(APlayerController* PC, bool bRootOnly, AVamCharacterActor*& OutActor, int32& OutBone, FVector& OutPosition) const
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
        const FVector RootWorld=RootControlPosition(Character);
        FVector2D RootScreen;
        if (PC->ProjectWorldLocationToScreen(RootWorld,RootScreen))
        {
            const float Distance=FVector2D::DistSquared(RootScreen,FVector2D(X,Y));
            if (Distance<Best) { Best=Distance; OutActor=Character; OutBone=INDEX_NONE; OutPosition=RootWorld; }
        }
        if (bRootOnly) continue;
        for (int32 I=0;I<Body->GetNumBones();++I)
        {
            if (!Character->Character->IsPoseControlBone(I)) continue;
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
    HoverActor.Reset(); HoverBone=INDEX_NONE;
    if (bShowBoneControls && !DragActor)
    {
        AVamCharacterActor* HitActor=nullptr; int32 HitBone=INDEX_NONE; FVector HitPosition;
        if (FindControlAtMouse(PC,false,HitActor,HitBone,HitPosition))
        { HoverActor=HitActor; HoverBone=HitBone; }
    }
    if (PC->WasInputKeyJustPressed(EKeys::LeftMouseButton) || PC->WasInputKeyJustPressed(EKeys::RightMouseButton))
    {
        DragActor=nullptr; DragBone=INDEX_NONE; bRootDrag=false; bRootRotationDrag=false;
        AVamCharacterActor* HitActor=nullptr; int32 Bone=INDEX_NONE; FVector Position;
        const bool bRight=PC->WasInputKeyJustPressed(EKeys::RightMouseButton);
        const bool bFound=FindControlAtMouse(PC,bRight,HitActor,Bone,Position);
        if (bFound && (!bRight || Bone==INDEX_NONE))
        {
            DragActor=HitActor; DragBone=Bone;
            Target=HitActor;
            bRootDrag=Bone==INDEX_NONE;
            bRootRotationDrag=bRootDrag && bRight;
            Anchor=Position; RootStart=DragActor->GetActorTransform();
            if (!bRootDrag) PoseStart=DragActor->Character->GetPoseControlRotation(Bone);
            if (bRootDrag) EnableRootPhysicalResponse(DragActor);
            float MouseX=0.f,MouseY=0.f;
            PC->GetMousePosition(MouseX,MouseY);
            MouseStart=FVector2D(MouseX,MouseY);
            FVector Origin,Direction;
            if (PC->DeprojectMousePositionToWorld(Origin,Direction)) PlaneNormal=Direction.GetSafeNormal();
        }
    }
    if (DragActor)
    {
        const bool bHeld=PC->IsInputKeyDown(bRootRotationDrag ? EKeys::RightMouseButton : EKeys::LeftMouseButton);
        if (!bHeld)
        {
            DragActor=nullptr; DragBone=INDEX_NONE; bRootDrag=false; bRootRotationDrag=false;
        }
        else if (bRootRotationDrag && DragActor->Motion)
        {
            float X=0.f,Y=0.f;
            if (PC->GetMousePosition(X,Y))
            {
                FTransform NewRoot=RootStart;
                const double Radians=FMath::DegreesToRadians((X-MouseStart.X)*RotationDegreesPerPixel);
                NewRoot.SetRotation((FQuat(FVector::UpVector,Radians)*RootStart.GetRotation()).GetNormalized());
                DragActor->Motion->MoveContinuously(NewRoot,GetWorld()->GetTimeSeconds());
            }
        }
        else if (bRootDrag && DragActor->Motion)
        {
            FVector Position;
            if (MouseOnPlane(PC,Position))
            {
                FTransform NewRoot=RootStart;
                NewRoot.AddToTranslation(Position-Anchor);
                DragActor->Motion->MoveContinuously(NewRoot,GetWorld()->GetTimeSeconds());
            }
        }
        else if (DragActor->Character && DragBone!=INDEX_NONE)
        {
            float X=0.f,Y=0.f;
            if (PC->GetMousePosition(X,Y))
            {
                const FVector2D Delta=FVector2D(X,Y)-MouseStart;
                FRotator Rotation=PoseStart;
                Rotation.Pitch-=Delta.Y*RotationDegreesPerPixel;
                if (PC->IsInputKeyDown(EKeys::LeftShift) || PC->IsInputKeyDown(EKeys::RightShift))
                    Rotation.Roll+=Delta.X*RotationDegreesPerPixel;
                else Rotation.Yaw+=Delta.X*RotationDegreesPerPixel;
                DragActor->Character->SetPoseControlRotation(DragBone,Rotation);
            }
        }
    }
    PC->CurrentMouseCursor=DragActor ? EMouseCursor::GrabHandClosed :
        HoverActor.IsValid() ? EMouseCursor::GrabHand : EMouseCursor::Default;
    if (bShowBoneControls)
    {
        TArray<AActor*> Actors;
        UGameplayStatics::GetAllActorsOfClass(GetWorld(),AVamCharacterActor::StaticClass(),Actors);
        for (AActor* Actor:Actors)
        {
            auto* Character=Cast<AVamCharacterActor>(Actor);
            if (!Character || !Character->Character || !Character->Character->Body) continue;
            USkeletalMeshComponent* Body=Character->Character->Body;
            const FVector Root=RootControlPosition(Character);
            const bool bRootPressed=Character==DragActor && bRootDrag;
            const bool bRootHovered=!bRootPressed && Character==HoverActor.Get() && HoverBone==INDEX_NONE;
            DrawDebugSphere(GetWorld(),Root,bRootPressed?5.5f:bRootHovered?4.5f:3.2f,12,
                bRootPressed?FColor::Yellow:bRootHovered?FColor::White:FColor::Orange,false,0.f,0,
                bRootPressed?2.8f:bRootHovered?2.f:1.1f);
            if (bRootPressed || bRootHovered)
            {
                DrawDebugSphere(GetWorld(),Root,bRootPressed?8.f:6.5f,16,
                    bRootPressed?FColor::Red:FColor::Orange,false,0.f,0,2.2f);
                DrawDebugString(GetWorld(),Root+FVector(0,0,9),
                    bRootPressed ? TEXT("ROOT | MOVING") : TEXT("ROOT | DRAG TO MOVE"),nullptr,
                    bRootPressed?FColor::Yellow:FColor::White,0.f,true,1.35f);
            }
            for (int32 I=0;I<Body->GetNumBones();++I)
            {
                if (!Character->Character->IsPoseControlBone(I)) continue;
                const FName Bone=Body->GetBoneName(I);
                const FVector Position=Body->GetBoneLocation(Bone);
                const bool bPressed=Character==DragActor && I==DragBone && !bRootDrag;
                const bool bHovered=!bPressed && Character==HoverActor.Get() && I==HoverBone;
                DrawDebugSphere(GetWorld(),Position,bPressed?4.5f:bHovered?3.4f:1.8f,12,
                    bPressed?FColor::Yellow:bHovered?FColor::White:FColor::Cyan,false,0.f,0,
                    bPressed?2.8f:bHovered?2.f:.8f);
                if (bPressed || bHovered)
                {
                    DrawDebugSphere(GetWorld(),Position,bPressed?7.f:5.5f,16,
                        bPressed?FColor::Magenta:FColor::Cyan,false,0.f,0,2.2f);
                    DrawDebugString(GetWorld(),Position+FVector(0,0,7),
                        FString::Printf(TEXT("%s | %s"),*Bone.ToString(),
                            bPressed?TEXT("ROTATING"):TEXT("DRAG TO ROTATE")),nullptr,
                        bPressed?FColor::Yellow:FColor::White,0.f,true,1.25f);
                }
            }
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
    if (!Target) return;
    if (PC->WasInputKeyJustPressed(EKeys::P) && Target->Motion) Target->Motion->SetPreviewPaused(!Target->Motion->GetClock().bPaused);
    if (PC->WasInputKeyJustPressed(EKeys::O) && Target->Motion) Target->Motion->StepPreview();
    if (PC->WasInputKeyJustPressed(EKeys::R) && Target->Motion) Target->Motion->ResetPreview();
    if (PC->WasInputKeyJustPressed(EKeys::G) && Target->Interaction) Target->Interaction->SetPhysicalMode(EVamPhysicalMode::Ragdoll);
    if (PC->WasInputKeyJustPressed(EKeys::C) && Target->Interaction) Target->Interaction->SetPhysicalMode(EVamPhysicalMode::Controlled);
}

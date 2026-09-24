#include "VamActivePoseComponent.h"
#include "VamCharacterComponent.h"
#include "VamRigProfile.h"
#include "VamShapeAnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"

UVamActivePoseComponent::UVamActivePoseComponent()
{
    PrimaryComponentTick.bCanEverTick=true;
    PrimaryComponentTick.TickGroup=TG_PrePhysics;
}
void UVamActivePoseComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime,TickType,ThisTickFunction);
    const double Time=GetWorld()->GetTimeSeconds();
    const double BlinkPhase=FMath::Fmod(Time,FMath::Max(.25f,BlinkIntervalSeconds));
    BlinkWeight=bBlink ? FMath::Clamp(float(1-FMath::Abs(BlinkPhase-.11)/.11),0.f,1.f) : 0.f;
    BreathValue=bBreathing ? FMath::Sin(float(Time*BreathsPerMinute*2*PI/60)+BreathPhase)*FMath::Clamp(BreathDepth,0.f,2.f) : 0.f;
    auto* Character=GetOwner()->FindComponentByClass<UVamCharacterComponent>();
    USkeletalMeshComponent* Body=Character ? Character->Body : nullptr;
    const UVamRigProfile* Rig=Character ? Character->RigProfile.Get() : nullptr;
    auto* Anim=Body ? Cast<UVamShapeAnimInstance>(Body->GetAnimInstance()) : nullptr;
    if (!Rig || !Anim) return;
    Anim->ClearActiveBoneOffsets();
    const float Breath=BreathValue;
    auto Rotate=[&](FName Semantic,const FRotator& Rotation,FVector Translation=FVector::ZeroVector)
    {
        const FName Bone=Rig->BoneForSemantic(Semantic);
        const int32 Index=Bone.IsNone() ? INDEX_NONE : Body->GetBoneIndex(Bone);
        if (Index!=INDEX_NONE) Anim->SetActiveBoneOffset(Index,FTransform(Rotation,Translation));
    };
    Rotate(TEXT("chest"),FRotator(Breath*1.2f,0,0),FVector(0,0,Breath*.45f));
    Rotate(TEXT("spine_upper"),FRotator(Breath*.4f,0,0),FVector(0,0,Breath*.2f));
    Rotate(TEXT("left_clavicle"),FRotator(0,0,Breath*.25f));
    Rotate(TEXT("right_clavicle"),FRotator(0,0,-Breath*.25f));
    const float Idle=bIdle ? FMath::Sin(float(Time*.7))*0.35f : 0.f;
    Rotate(TEXT("head"),FRotator(Idle,Idle*.5f,0));
    Rotate(TEXT("eye_l"),FRotator(EyeGazeDegrees.Y,EyeGazeDegrees.X,0));
    Rotate(TEXT("eye_r"),FRotator(EyeGazeDegrees.Y,EyeGazeDegrees.X,0));
    Rotate(TEXT("jaw"),FRotator(FMath::Clamp(JawOpenDegrees,0.f,35.f),0,0));
    TMap<FName,float> Expressions;
    if (bBlink)
    {
        if (!BlinkMorphTarget.IsNone()) Expressions.Add(BlinkMorphTarget,BlinkWeight);
        for (const FName Target:BlinkMorphTargets)
            if (!Target.IsNone()) Expressions.Add(Target,BlinkWeight);
    }
    // Character owns the final Morph writes for body and followers. An unsupported
    // mapping clears the old active layer rather than retaining a stale blink.
    if (!Character->SetExpressionWeights(Expressions)) Character->SetExpressionWeights({});
}

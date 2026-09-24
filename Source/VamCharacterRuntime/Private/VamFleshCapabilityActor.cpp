#include "VamFleshCapabilityActor.h"
#include "VamFleshCapabilityAsset.h"
#include "VamCharacterComponent.h"
#include "VamActivePoseComponent.h"
#include "VamShapeAnimInstance.h"
#include "ChaosFlesh/FleshComponent.h"
#include "ChaosFlesh/FleshDynamicAsset.h"
#include "ChaosFlesh/ChaosDeformableSolverComponent.h"
#include "ChaosFlesh/ChaosDeformableCollisionsComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Animation/MeshDeformer.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "UObject/ConstructorHelpers.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "UnrealClient.h"

AVamFleshCapabilityActor::AVamFleshCapabilityActor()
{
    PrimaryActorTick.bCanEverTick=true;PrimaryActorTick.TickGroup=TG_PostUpdateWork;
    FleshSolver=CreateDefaultSubobject<UDeformableSolverComponent>(TEXT("FleshSolver"));FleshSolver->SetupAttachment(RootComponent);
    FleshSolver->SolverTiming.NumSubSteps=4;FleshSolver->SolverTiming.NumSolverIterations=10;
    FleshSolver->SolverTiming.bDoThreadedAdvance=false;FleshSolver->SolverForces.bEnableGravity=false;
    FleshSolver->SolverCollisions.bUseFloor=false;
    Flesh=CreateDefaultSubobject<UFleshComponent>(TEXT("Flesh"));Flesh->SetupAttachment(RootComponent);Flesh->SetVisibility(false);
    FleshCollisions=CreateDefaultSubobject<UDeformableCollisionsComponent>(TEXT("FleshCollisions"));FleshCollisions->SetupAttachment(RootComponent);
    ProbeSphere=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ContactSphere"));ProbeSphere->SetupAttachment(RootComponent);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere"));ProbeSphere->SetStaticMesh(Sphere.Object);
    ProbeSphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);ProbeSphere->SetVisibility(false);
    SurfaceDeformer=FSoftObjectPath(TEXT("/ChaosFlesh/Deformers/DG_FleshDeformer.DG_FleshDeformer"));
}
void AVamFleshCapabilityActor::Finish(const FString& Reason)
{
    TSharedRef<FJsonObject> Report=MakeShared<FJsonObject>();
    Report->SetStringField(TEXT("scope"),TEXT("Chaos Flesh closed cage and native skin binding experiment; not production tissue"));
    Report->SetStringField(TEXT("reason"),Reason);Report->SetBoolField(TEXT("stage07_passed"),false);
    Report->SetBoolField(TEXT("backend_qualified"),false);
    Report->SetNumberField(TEXT("bound_render_vertices"),Capability.IsValid()?Capability.Get()->BoundRenderVertices:0);
    Report->SetNumberField(TEXT("surface_probe_peak_cm"),SurfacePeak);
    Report->SetNumberField(TEXT("maximum_relative_volume_error"),VolumeErrorPeak);
    Report->SetNumberField(TEXT("measured_frames"),MeasuredFrames);
    FString Json;FJsonSerializer::Serialize(Report,TJsonWriterFactory<>::Create(&Json));
    FFileHelper::SaveStringToFile(Json,*(FPaths::ProjectSavedDir()/TEXT("FleshCapability.json")));
    SetActorTickEnabled(false);FPlatformMisc::RequestExit(false);
}
void AVamFleshCapabilityActor::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if(!FParse::Param(FCommandLine::Get(),TEXT("VamFleshCapability"))) return;
    if(!Started)
    {
        if(!Character->Body) return;
        auto* Asset=Capability.LoadSynchronous();auto* Deformer=SurfaceDeformer.LoadSynchronous();
        if(!Asset || !Asset->Flesh || !Deformer || Asset->Body!=Character->Body->GetSkeletalMeshAsset()) {Finish(TEXT("incompatible_or_missing_capability_asset"));return;}
        ActivePose->bBreathing=ActivePose->bBlink=ActivePose->bIdle=false;
        if(auto* Anim=Cast<UVamShapeAnimInstance>(Character->Body->GetAnimInstance())) Anim->SetBaseAnimation(nullptr);
        TArray<USkeletalMeshComponent*> Meshes;GetComponents(Meshes);for(auto* Mesh:Meshes) if(Mesh!=Character->Body) Mesh->SetVisibility(false);
        Flesh->SetRestCollection(Asset->Flesh);Flesh->BodyForces.bApplyGravity=false;
        Flesh->EnableSimulation(FleshSolver);FleshCollisions->EnableSimulation(FleshSolver);
        Flesh->AddTickPrerequisiteComponent(Character->Body);AddTickPrerequisiteComponent(FleshSolver);
        for(int32 Slot=0;Slot<Asset->SurfaceMaterials.Num();++Slot)
            Character->Body->SetMaterial(Slot,Asset->SurfaceMaterials[Slot]);
        Character->Body->SetMeshDeformer(Deformer);
        FBox Bounds(ForceInit);for(const auto& V:Asset->RestVertices) Bounds+=V;
        ProbeRadius=Bounds.GetSize().GetMin()*.3f;
        ProbeStart=Bounds.GetCenter();ProbeStart.X=Bounds.Max.X+ProbeRadius+2;
        ProbeSphere->SetRelativeLocation(ProbeStart);ProbeSphere->SetRelativeScale3D(FVector(ProbeRadius/50));ProbeSphere->SetVisibility(true);
        FleshCollisions->AddStaticMeshComponent(ProbeSphere);
        StartTime=GetWorld()->GetTimeSeconds();Started=true;return;
    }
    const double Time=GetWorld()->GetTimeSeconds()-StartTime;
    const float Penetration=Time<4?0:Time<8?float(Time-4)/4:Time<12?1:Time<16?float(16-Time)/4:0;
    ProbeSphere->SetRelativeLocation(ProbeStart-FVector(Penetration*(ProbeRadius+4),0,0));
    const auto* Asset=Capability.Get();const auto* Dynamic=Flesh->GetDynamicCollection();
    const auto* Positions=Dynamic?Dynamic->FindPositions():nullptr;
    if(Positions && Positions->Num()==Asset->RestVertices.Num())
    {
        ++MeasuredFrames;double Volume=0;
        for(const auto& T:Asset->Tetrahedra)
        {
            const FVector A((*Positions)[T[0]]),B((*Positions)[T[1]]),C((*Positions)[T[2]]),D((*Positions)[T[3]]);
            Volume+=FVector::DotProduct(B-A,FVector::CrossProduct(C-A,D-A))/6;
        }
        VolumeErrorPeak=FMath::Max(VolumeErrorPeak,float(FMath::Abs(Volume/Asset->RestVolumeCm3-1)));
        for(int32 V=0;V<Asset->SurfaceMask.Num();++V) if(Asset->SurfaceMask[V]>.5)
        {
            FVector Delta=FVector::ZeroVector;const auto& T=Asset->SurfaceParents[V];const auto& W=Asset->SurfaceWeights[V];
            for(int32 J=0;J<4;++J) Delta+=(FVector((*Positions)[T[J]])-Asset->RestVertices[T[J]])*W[J];
            if(Delta.ContainsNaN()) {Finish(TEXT("nonfinite_surface"));return;}
            SurfacePeak=FMath::Max(SurfacePeak,float(Delta.Size())*Asset->SurfaceMask[V]);
        }
    }
    const double Times[3]={3,10,19};
    if(ScreenshotStage<3 && Time>Times[ScreenshotStage])
    {FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/FString::Printf(TEXT("FleshCapability-%d.png"),ScreenshotStage),false,false);++ScreenshotStage;}
    if(Time>20) Finish(TEXT("capture_complete_pending_visual_and_contact_review"));
}

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Containers/Ticker.h"
#include "Tickable.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "VamRuntimeConfiguration.h"
#include "VamCharacterDefinition.h"
#include "VamBreastJiggleBuilder.h"
#include "VamBreastSkeletalMeshComponent.h"
#include "VamCharacterActor.h"
#include "VamCharacterComponent.h"
#include "VamMotionComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Engine/SkeletalMesh.h"
#include "UObject/UObjectGlobals.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVamBreastNativeTest,"Vam.Breast.NativeRuntime",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FVamBreastNativeTest::RunTest(const FString& Parameters)
{
    FString Path;
    if(!FParse::Value(FCommandLine::Get(),TEXT("VamBreastTestConfig="),Path)) { AddError(TEXT("Pass -VamBreastTestConfig=/Game/.../RC_Runtime for a committed output"));return false; }
    auto* Config=LoadObject<UVamRuntimeConfiguration>(nullptr,*Path);
    if(!TestNotNull(TEXT("Runtime config reload"),Config)) return false;
    auto* Definition=Config->Definition.LoadSynchronous();auto* Profile=Config->BreastJiggle.LoadSynchronous();
    if(!TestNotNull(TEXT("Profile reload"),Profile) || !TestNotNull(TEXT("Definition reload"),Definition)) return false;
    TestEqual(TEXT("Persisted helper/weight validation"),UVamBreastJiggleBuilder::Validate(Definition,Profile),FString());
    const auto IVS=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true).RequiresHitProxies(false).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(true).SetTransactional(false);
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&IVS);
    FWorldContext& Context=GEngine->CreateNewWorldContext(EWorldType::Game);Context.SetCurrentWorld(World);
    World->InitializeActorsForPlay(FURL());World->GetWorldSettings()->NotifyBeginPlay();
    TestTrue(TEXT("Isolated game world begun play"),World->HasBegunPlay());
    AVamCharacterActor* A=World->SpawnActor<AVamCharacterActor>();
    AVamCharacterActor* B=World->SpawnActor<AVamCharacterActor>();
    A->Character->RuntimeConfiguration=Config;B->Character->RuntimeConfiguration=Config;
    A->LoadCharacter();B->LoadCharacter();
    auto Tick=[&](){++GFrameCounter;FTSTicker::GetCoreTicker().Tick(1.f/60.f);FTickableGameObject::TickObjects(nullptr,LEVELTICK_All,false,1.f/60.f);World->Tick(LEVELTICK_All,1.f/60.f);};
    for(int32 I=0;I<30;++I) { FlushAsyncLoading();Tick(); }
    auto* MA=Cast<UVamBreastSkeletalMeshComponent>(A->Character->Body);
    auto* MB=Cast<UVamBreastSkeletalMeshComponent>(B->Character->Body);
    bool Ready=TestNotNull(TEXT("Native A body"),MA) && TestNotNull(TEXT("Native B body"),MB);
    if(Ready)
    {
        for(int32 I=0;I<120;++I) Tick();
        TestEqual(TEXT("Both sides initialized"),MA->Solvers.Num(),2);
        AddInfo(MA->BreastDiagnostics());
        if(MA->Solvers.Num()==2 && MB->Solvers.Num()==2 && TestEqual(TEXT("Five live dynamic nodes"),MA->Solvers[0].Nodes.Num(),5) && MB->Solvers[0].Nodes.Num()==5)
        {
            B->Motion->SetPreviewPaused(true);
            const auto RestB=MB->Solvers[0].Nodes;
            for(int32 I=0;I<30;++I) { A->SetActorLocation(FVector(.5*200*FMath::Square((I+1)/60.),0,0));Tick(); }
            TestTrue(TEXT("Final animated anchor produces nonzero inertia"),MA->Solvers[0].Nodes[0].Displacement.Size()>.005);
            TestTrue(TEXT("Paused peer state isolation"),(MB->Solvers[0].Nodes[0].Displacement-RestB[0].Displacement).Size()<.01);
            MA->ResetBreastJiggle();
            for(int32 I=0;I<40;++I)
            {
                A->Character->SetDebugBoneOffset(Profile->Sides[0].ChestBone,FTransform(FQuat(FVector::UpVector,.15*FMath::Sin(I*.15)),FVector::ZeroVector));Tick();
            }
            TestTrue(TEXT("Final chest pose sampled even with stationary Actor"),MA->Solvers[0].AngularVelocity.Size()>.01);
            A->Character->ResetDebugBoneOffsets();
            auto* Skeleton=MA->GetSkeletalMeshAsset()->GetSkeleton();
            const auto Identity=MA->GetSkeletalMeshAsset()->GetRefSkeleton().GetRawRefBoneInfo();
            if(!Profile->Sides[0].ShapeResponses.IsEmpty())
            {
                const auto& Response=Profile->Sides[0].ShapeResponses[0];
                A->Character->SetParameter(Response.Parameter,Response.DefaultValue+.01);A->Character->CommitShape();Tick();
                TestTrue(TEXT("Shape keeps mesh/skeleton identity"),Skeleton==MA->GetSkeletalMeshAsset()->GetSkeleton() && Identity.Num()==MA->GetSkeletalMeshAsset()->GetRefSkeleton().GetRawBoneNum());
            }
            A->Motion->TeleportTo(FTransform(FVector(10000,0,0)),World->GetTimeSeconds());Tick();
            TestTrue(TEXT("Actual TeleportRevision clears impulse"),MA->Solvers[0].Nodes[0].Velocity.Size()<.01);
            A->Motion->SetPreviewPaused(true);const auto Frozen=MA->Solvers[0].Nodes[0];
            for(int32 I=0;I<10;++I) Tick();
            TestTrue(TEXT("Actual pause frozen"),MA->Solvers[0].Nodes[0].Displacement.Equals(Frozen.Displacement,1.e-8));
            A->Motion->SetPreviewPaused(false);Tick();
            TestTrue(TEXT("Resume bounded"),MA->Solvers[0].Nodes[0].Velocity.Size()<100);
        }
        A->Destroy();B->Destroy();Tick();
    }
    World->BeginTearingDown();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);
    return true;
}
#endif

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonReader.h"
#include "VamGluteJiggleBuilder.h"
#include "VamGluteStructureBuilder.h"
#include "VamGluteSkeletalMeshComponent.h"
#include "VamGluteSurfaceSnapshot.h"
#include "VamRuntimeConfiguration.h"
#include "VamCharacterActor.h"
#include "VamCharacterComponent.h"
#include "VamCharacterDefinition.h"
#include "VamMotionComponent.h"
#include "Containers/Ticker.h"
#include "Tickable.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/WorldSettings.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVamGluteSpatialTest,"Vam.Glute.G1.SpatialCalibration",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FVamGluteSpatialTest::RunTest(const FString& Parameters)
{
    FString Path;FParse::Value(FCommandLine::Get(),TEXT("VamGluteTestConfig="),Path);
    auto* C=LoadObject<UVamRuntimeConfiguration>(nullptr,*Path);if(!TestNotNull(TEXT("Configuration"),C)) return false;
    auto* P=C->GluteStructure.LoadSynchronous();auto* D=C->Definition.LoadSynchronous();
    if(!TestNotNull(TEXT("Structure"),P) || !TestNotNull(TEXT("Definition"),D)) return false;
    TestTrue(TEXT("Surface quadrature calibration"),P->Algorithm.StartsWith(TEXT("glute-structure-g05-surface-")));
    TestEqual(TEXT("Spatial / bind / weight validation"),UVamGluteStructureBuilder::Validate(D,P),FString());
    TSharedPtr<FJsonObject> Audit;
    if(!TestTrue(TEXT("Spatial audit JSON"),FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(UVamGluteStructureBuilder::SpatialAudit(D,P)),Audit))) return false;
    TestTrue(TEXT("Helpers match actual influenced surface"),Audit->GetBoolField(TEXT("valid")));
    for(const auto& S:P->Sides)
    {
        const auto Side=Audit->GetObjectField(S.Side.ToString());
        TestEqual(TEXT("Unused vertices cannot carry region evidence"),Side->GetNumberField(TEXT("unused_region_weight")),0.);
        TestTrue(TEXT("Upper / Lower semantic spread"),S.Regions[1].Rest.Z>S.Regions[2].Rest.Z);
        TestTrue(TEXT("Medial / Lateral mirrored semantic spread"),S.SideSign*(S.Regions[4].Rest.Y-S.Regions[3].Rest.Y)>0);
        for(const auto& N:Side->GetArrayField(TEXT("nodes"))) AddInfo(FString::Printf(TEXT("%s %s helper/support distance %.6f cm, normalized %.6f"),*S.Side.ToString(),*N->AsObject()->GetStringField(TEXT("semantic")),N->AsObject()->GetNumberField(TEXT("support_error_cm")),N->AsObject()->GetNumberField(TEXT("support_error_normalized"))));
    }
    // A deliberately displaced helper must be rejected; this catches the old
    // failure even when physics and zero-offset bind reconstruction still pass.
    auto* Broken=DuplicateObject<UVamGluteStructureProfile>(P,GetTransientPackage());
    Broken->Sides[0].Regions[0].Rest+=Broken->Sides[0].Dimensions*2;
    TestTrue(TEXT("Bad spatial calibration cannot publish"),UVamGluteStructureBuilder::Validate(D,Broken).Contains(TEXT("spatial calibration invalid")));
    TSharedPtr<FJsonObject> Transfer;
    if(TestTrue(TEXT("Native coherent unit-translation audit"),FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(UVamGluteStructureBuilder::SurfaceTransferAudit(D,P,C->BreastJiggle.LoadSynchronous())),Transfer)))
    {
        for(const auto& S:P->Sides)
        {
            const auto Stats=Transfer->GetObjectField(TEXT("Glute_")+S.Side.ToString());
            const double Mean=Stats->GetNumberField(TEXT("mean_transfer")),Core=Stats->GetNumberField(TEXT("core_transfer"));
            AddInfo(FString::Printf(TEXT("G1 surface transfer %s mean %.6f core %.6f"),*S.Side.ToString(),Mean,Core));
            if(P->Algorithm==TEXT("glute-structure-g05-surface-v3"))
            {
                TestTrue(TEXT("Interior tissue participates without hiding node motion"),Mean>.25 && Core>.35);
                TestTrue(TEXT("Original donor support retained"),Stats->GetNumberField(TEXT("max_transfer"))<=P->SkinTransferMaximum+.0002);
                TestTrue(TEXT("Low confidence edge remains faded"),Stats->GetNumberField(TEXT("p10_transfer"))<Core*.5);
            }
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVamGluteG1AssetTest,"Vam.Glute.G1.CharacterCalibration",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FVamGluteG1AssetTest::RunTest(const FString& Parameters)
{
    FString Path;if(!FParse::Value(FCommandLine::Get(),TEXT("VamGluteTestConfig="),Path)) {AddError(TEXT("Pass -VamGluteTestConfig"));return false;}
    auto* C=LoadObject<UVamRuntimeConfiguration>(nullptr,*Path);if(!TestNotNull(TEXT("Configuration reload"),C)) return false;
    auto* G=C->GluteStructure.LoadSynchronous();if(!TestNotNull(TEXT("G0 measured geometry"),G)) return false;
    auto* P=C->GluteJiggle.LoadSynchronous();
    if(!P) {P=NewObject<UVamGluteJiggleProfile>();TestEqual(TEXT("Calibrate baseline G0 fixture"),UVamGluteJiggleBuilder::Build(G,P),FString());}
    TestTrue(TEXT("G1 immutable calibration valid"),P->IsValidProfile());
    for(int32 Side=0;Side<2;++Side)
    {
        const auto& S=G->Sides[Side];auto Neutral=VamGluteStructure::Evaluate(*G,S,S.RestThighInAnchor);
        const auto R=VamGluteDynamics::Calibrate(*P,S,Neutral);
        auto Flex=S.RestThighInAnchor;Flex.SetRotation(FQuat(FVector::YAxisVector,PI/2)*Flex.GetRotation());
        const auto Deep=VamGluteStructure::Evaluate(*G,S,Flex);const auto Tense=VamGluteDynamics::Calibrate(*P,S,Deep);
        TestEqual(TEXT("Pose tension cannot change mass"),R.MassKg,Tense.MassKg);
        TestTrue(TEXT("Core passive tension raises support"),Tense.Nodes[0].Support.GetMin()>R.Nodes[0].Support.GetMin());
        TestTrue(TEXT("Regional response differs"),!(Tense.Nodes[0].Support/R.Nodes[0].Support).Equals(Tense.Nodes[2].Support/R.Nodes[2].Support,1.e-5));
        for(int32 I=0;I<5;++I) TestTrue(TEXT("Tension keeps mass/travel/helper identity"),R.Nodes[I].MassKg==Tense.Nodes[I].MassKg && R.Nodes[I].PositiveTravel==Tense.Nodes[I].PositiveTravel && R.Nodes[I].BoneIndex==Tense.Nodes[I].BoneIndex);
        FVamGluteSolver Solver;double Peak[5]={},Cost=0;
        for(int32 Frame=0;Frame<1200;++Frame)
        {
            const double Time=Frame/120.;auto Thigh=S.RestThighInAnchor;Thigh.SetRotation(FQuat(FVector::YAxisVector,.4*FMath::Sin(Time*4))*Thigh.GetRotation());
            const auto State=VamGluteStructure::Evaluate(*G,S,Thigh);const auto Current=VamGluteDynamics::Calibrate(*P,S,State);
            FVamGluteMotion M;M.Thigh=Thigh;Solver.Advance(*P,Current,M,1./120.,FVector(0,0,-980),FVamGluteTuning());Cost+=Solver.LastCostMicroseconds;
            for(int32 I=0;I<5;++I) Peak[I]=FMath::Max(Peak[I],Solver.Nodes[I].Displacement.Size());
        }
        TestTrue(TEXT("Measured lower/lateral moving attachment response"),Peak[2]>.001 && Peak[4]>.001);
        TestTrue(TEXT("Measured upper/medial are more constrained"),Peak[2]>Peak[1] && Peak[4]>Peak[3]);
        AddInfo(FString::Printf(TEXT("%s %s volume %.6f mass %.6f thigh peaks %.6f %.6f %.6f %.6f %.6f cost %.3f us"),*Path,*S.Side.ToString(),S.EffectiveVolumeCm3,R.MassKg,Peak[0],Peak[1],Peak[2],Peak[3],Peak[4],Cost/1200));
        for(const FVector Angles:{FVector::ZeroVector,FVector(PI/3,0,0),FVector(PI/2,0,0),FVector(-.4,0,0),FVector(0,.5,0),FVector(1.2,0,.4)})
        {
            auto Thigh=S.RestThighInAnchor;Thigh.SetRotation(FQuat(FVector::YAxisVector,Angles.X)*FQuat(FVector::XAxisVector,Angles.Y)*FQuat(S.FemurAxisInAnchor,Angles.Z)*Thigh.GetRotation());
            const auto State=VamGluteStructure::Evaluate(*G,S,Thigh);const auto Current=VamGluteDynamics::Calibrate(*P,S,State);Solver.Reset();
            FVamGluteMotion M;M.Thigh=Thigh;Solver.Advance(*P,Current,M,0,FVector(0,0,-980),FVamGluteTuning());
            for(auto& N:Solver.Nodes) { N.PositionWorld+=FVector(.05,0,.05);N.VelocityWorld=FVector(1,0,0); }
            for(int32 Frame=0;Frame<2400;++Frame) Solver.Advance(*P,Current,M,1./120.,FVector(0,0,-980),FVamGluteTuning());
            for(const auto& N:Solver.Nodes) TestTrue(TEXT("All fixed poses settle to G0.5 rest"),N.Displacement.Size()<1.e-5 && N.RelativeVelocity.Size()<1.e-5);
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVamGluteG1NativeTest,"Vam.Glute.G1.NativeSurface",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FVamGluteG1NativeTest::RunTest(const FString& Parameters)
{
    FString Path;if(!FParse::Value(FCommandLine::Get(),TEXT("VamGluteTestConfig="),Path)) {AddError(TEXT("Pass -VamGluteTestConfig"));return false;}
    auto* C=LoadObject<UVamRuntimeConfiguration>(nullptr,*Path);if(!TestNotNull(TEXT("Configuration"),C) || !TestNotNull(TEXT("Reloaded G1 asset"),C->GluteJiggle.LoadSynchronous())) return false;
    const auto IVS=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true).RequiresHitProxies(false).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(true).SetTransactional(false);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&IVS);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);World->InitializeActorsForPlay(FURL());World->GetWorldSettings()->NotifyBeginPlay();
    auto* A=World->SpawnActor<AVamCharacterActor>();auto* B=World->SpawnActor<AVamCharacterActor>();
    A->Character->RuntimeConfiguration=C;B->Character->RuntimeConfiguration=C;A->LoadCharacter();B->LoadCharacter();
    auto Tick=[&](double Dt){++GFrameCounter;FTSTicker::GetCoreTicker().Tick(Dt);FTickableGameObject::TickObjects(nullptr,LEVELTICK_All,false,Dt);World->Tick(LEVELTICK_All,Dt);};
    for(int32 I=0;I<30;++I){FlushAsyncLoading();Tick(1./60);}
    auto* MA=Cast<UVamGluteSkeletalMeshComponent>(A->Character->Body);auto* MB=Cast<UVamGluteSkeletalMeshComponent>(B->Character->Body);
    auto Evidence=MakeShared<FJsonObject>();
    if(TestNotNull(TEXT("Ordinary character A"),MA) && TestNotNull(TEXT("Ordinary character B"),MB))
    {
        TestTrue(TEXT("Both instances loaded same immutable G1 profile"),MA->GluteJiggleProfile==MB->GluteJiggleProfile);
        auto Snapshot=[&](const FString& Name)->double
        {
            const FString Result=CaptureGluteSurface(*MA,false,true);FString File;Result.Split(TEXT("快照已保存："),nullptr,&File);FString Json;TSharedPtr<FJsonObject> Data;
            if(!FFileHelper::LoadFileToString(Json,*File) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Data)) {AddError(TEXT("G1 surface snapshot unavailable: ")+Result);return -1;}
            Evidence->SetObjectField(Name,Data);double Max=0;for(const auto& Side:MA->GluteRest) Max=FMath::Max(Max,Data->GetObjectField(Side.Side.ToString())->GetObjectField(TEXT("whole"))->GetNumberField(TEXT("max")));return Max;
        };
        for(const FName Command:{FName(TEXT("Neutral standing")),FName(TEXT("Flexion 60")),FName(TEXT("Flexion 90")),FName(TEXT("Hip extension")),FName(TEXT("Abduction")),FName(TEXT("FlexExternal"))})
        {
            MA->GlutePoseCommand(Command);
            if(Command==TEXT("FlexExternal")) for(const auto& Side:MA->GluteRest)
                A->Character->SetDebugBoneOffset(Side.ThighBone,FTransform(FQuat(Side.AnchorLocal.TransformVectorNoScale(FVector::YAxisVector),PI/3)*FQuat(Side.AnchorLocal.TransformVectorNoScale(Side.FemurAxisInAnchor),Side.SideSign*PI/6)));
            for(int32 I=0;I<600;++I) Tick(1./120);
            const auto Weights=MA->CorrectiveWeights;const auto On=MA->GetComponentSpaceTransforms();const double Delta=Snapshot(Command.ToString());
            TestTrue(TEXT("Settled native surface equals G1 OFF"),Delta>=0 && Delta<1.e-4);
            MA->bGluteJiggleEnabled=false;Tick(1./120);
            for(const auto& W:Weights) TestEqual(TEXT("G1 switch does not change corrective weights"),MA->CorrectiveWeights.FindRef(W.Key),W.Value);
            for(const auto& Side:MA->GluteRest) for(const auto& R:Side.Regions) TestTrue(TEXT("Settled helper pose equals disabled"),On[R.BoneIndex].Equals(MA->GetComponentSpaceTransforms()[R.BoneIndex],1.e-4));
            MA->bGluteJiggleEnabled=true;Tick(1./120);
        }
        MA->GlutePoseCommand(TEXT("Flexion 90"));Tick(1./120);const auto Weights=MA->CorrectiveWeights;
        double Peak=0;
        for(int32 I=0;I<120;++I) {const double Time=(I+1)/120.;A->SetActorLocation(FVector(100*Time*Time,0,0));Tick(1./120);Peak=FMath::Max(Peak,MA->GluteSolvers[0].Nodes[0].Displacement.Size());}
        TestTrue(TEXT("Final primary actor acceleration reaches G1"),Peak>.001);
        const double Surface=Snapshot(TEXT("DeepFlexAcceleration"));TestTrue(TEXT("Morph-corrected surface follows dynamic helpers"),Surface>.001);
        for(const auto& W:Weights) TestEqual(TEXT("Acceleration does not change corrective weights"),MA->CorrectiveWeights.FindRef(W.Key),W.Value);
        TestTrue(TEXT("Second instance remains at rest"),MB->GluteSolvers[0].Nodes[0].Displacement.Size()<1.e-6);
        AddInfo(FString::Printf(TEXT("G1 native dynamic helper peak %.6f cm surface max %.6f cm"),Peak,Surface));
        // Exercise the same commands used by the panel and measure the skinned
        // surface, rather than treating moving debug points as proof of output.
        for(const FName Command:{FName(TEXT("Walk Cycle / Alternating Thigh Swing")),FName(TEXT("Jump")),FName(TEXT("Smooth Turn"))})
        {
            MA->GluteMotionCommand(TEXT("Reset"));Tick(1./60);MA->GluteMotionCommand(Command);
            double SurfacePeak=0,NodePeak=0;
            for(int32 Frame=0;Frame<240;++Frame)
            {
                Tick(1./60);for(const auto& Solver:MA->GluteSolvers) for(const auto& N:Solver.Nodes) NodePeak=FMath::Max(NodePeak,N.Displacement.Size());
                if(Frame%12==0) SurfacePeak=FMath::Max(SurfacePeak,Snapshot(Command.ToString()+FString::Printf(TEXT("_%03d"),Frame)));
            }
            TestTrue(TEXT("Panel motion reaches actual native surface"),SurfacePeak>.01);
            AddInfo(FString::Printf(TEXT("G1 panel %s node peak %.6f cm sampled surface peak %.6f cm"),*Command.ToString(),NodePeak,SurfacePeak));
        }
        MA->GluteMotionCommand(TEXT("Reset"));Tick(1./60);
        auto* Motion=A->FindComponentByClass<UVamMotionComponent>();const auto Frozen=MA->GluteSolvers[0];Motion->SetPreviewPaused(true);
        for(int32 I=0;I<30;++I) Tick(1./60);
        TestTrue(TEXT("Native pause freezes world particle"),MA->GluteSolvers[0].Nodes[0].PositionWorld==Frozen.Nodes[0].PositionWorld && MA->GluteSolvers[0].Nodes[0].VelocityWorld==Frozen.Nodes[0].VelocityWorld);
        Motion->SetPreviewPaused(false);Tick(1./60);
        Motion->TeleportTo(FTransform(FVector(10000,0,0)),World->GetTimeSeconds());Tick(1./120);
        TestTrue(TEXT("Native TeleportRevision clears residual"),MA->GluteSolvers[0].Nodes[0].Displacement.Size()<1.e-6);
        const auto* Skeleton=MA->GetSkeletalMeshAsset()->GetSkeleton();const int32 BoneCount=MA->GetSkeletalMeshAsset()->GetRefSkeleton().GetRawBoneNum();const double PeerMass=MB->GluteDynamics[0].MassKg;
        const auto* Definition=C->Definition.Get();FName Parameter;float Value=0;double Largest=0;
        for(const auto& Response:MA->GluteProfile->Sides[0].ShapeResponses)
            if(const auto* Param=Definition->Parameters.FindByPredicate([&](const FVamMorphParameter& X){return X.Target==Response.Parameter;}))
            {
                const float Candidate=Response.LogVolume>=0?Param->Maximum:Param->Minimum;const double Change=Response.LogVolume*(Candidate-Response.DefaultValue);
                if(Change>Largest) {Largest=Change;Parameter=Response.Parameter;Value=FMath::Lerp(Response.DefaultValue,Candidate,.5f);}
            }
        TestTrue(TEXT("Measured Shape response available"),Largest>0);
        if(Largest>0)
        {
            TMap<FName,float> Values;Values.Add(Parameter,Value);TestTrue(TEXT("Shape preview accepted"),A->Character->PreviewParameters(Values));Tick(1./120);
            TestTrue(TEXT("Shape recalibrates mass per instance"),MA->GluteDynamics[0].MassKg!=PeerMass && MB->GluteDynamics[0].MassKg==PeerMass);
            TestTrue(TEXT("Shape preview does not create motion impulse"),MA->GluteSolvers[0].Nodes[0].Displacement.Size()<1.e-5);
            TestTrue(TEXT("Shape commit accepted"),A->Character->CommitShape());Tick(1./120);
            TestTrue(TEXT("Shape commit preserves skeleton/helper identity"),Skeleton==MA->GetSkeletalMeshAsset()->GetSkeleton() && BoneCount==MA->GetSkeletalMeshAsset()->GetRefSkeleton().GetRawBoneNum());
        }
    }
    FString Report;if(FParse::Value(FCommandLine::Get(),TEXT("VamGluteG1Report="),Report)) { FString Json;auto Writer=TJsonWriterFactory<>::Create(&Json);FJsonSerializer::Serialize(Evidence,Writer);TestTrue(TEXT("Save G1 surface evidence"),FFileHelper::SaveStringToFile(Json,*Report)); }
    A->Destroy();B->Destroy();Tick(1./60);World->EndPlay(EEndPlayReason::Quit);World->DestroyWorld(false);GEngine->DestroyWorldContext(World);return true;
}
#endif

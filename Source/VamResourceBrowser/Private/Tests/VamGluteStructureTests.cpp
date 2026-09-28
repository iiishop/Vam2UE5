#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "VamGluteStructure.h"
#include "VamBreastJiggleBuilder.h"
#include "VamNativeBuilder.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Rendering/SkeletalMeshLODModel.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "VamGluteStructureBuilder.h"
#include "VamGluteSkeletalMeshComponent.h"
#include "VamRuntimeConfiguration.h"
#include "VamCharacterDefinition.h"
#include "VamCharacterActor.h"
#include "VamCharacterComponent.h"
#include "Containers/Ticker.h"
#include "Tickable.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/WorldSettings.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVamGlutePoseTest,"Vam.Glute.PoseFunction",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FVamGlutePoseTest::RunTest(const FString& Parameters)
{
    auto* P=NewObject<UVamGluteStructureProfile>();
    FVamGluteSide S;S.SideSign=1;S.RestThighInAnchor=FTransform(FVector(0,8,-2));
    for(int32 I=0;I<5;++I)
    {
        FVamGluteRegion R;R.Rest=FVector(9,8+(I-2)*1.,-3+(I-2)*2.);R.PelvisPoint=FVector(0,R.Rest.Y,R.Rest.Z);
        R.ThighPointLocal=FVector(2,1,-5);R.PelvisAttachment=.7-I*.05;R.ThighAttachment=1-R.PelvisAttachment;R.SupportBaseline=10;S.Regions.Add(R);
    }
    auto Neutral=VamGluteStructure::Evaluate(*P,S,S.RestThighInAnchor);
    for(int32 I=0;I<5;++I) TestTrue(TEXT("Neutral reconstructs imported rest"),Neutral.Regions[I].Transform.GetLocation().Equals(S.Regions[I].Rest,1.e-8));
    for(int32 Axis=0;Axis<3;++Axis)
    {
        FVector Direction=FVector::ZeroVector;Direction[Axis]=1;
        FVamGluteStructuralState Previous;
        for(int32 Degrees=-40;Degrees<=130;++Degrees)
        {
            auto T=S.RestThighInAnchor;T.SetRotation(FQuat(Direction,FMath::DegreesToRadians(double(Degrees))));
            const auto State=VamGluteStructure::Evaluate(*P,S,T);
            for(int32 I=0;I<5;++I)
            {
                const auto& R=State.Regions[I];const FVector Scale=R.Transform.GetScale3D();
                TestFalse(TEXT("Pose sweep finite"),R.Transform.ContainsNaN());
                TestTrue(TEXT("Positive normalized continuous attachment"),FMath::IsFinite(R.Tension) && R.PelvisAttachment>0 && R.ThighAttachment>0 && FMath::Abs(R.PelvisAttachment+R.ThighAttachment-1)<1.e-12);
                TestTrue(TEXT("Regional determinant one"),FMath::Abs(Scale.X*Scale.Y*Scale.Z-1)<1.e-12);
                TestTrue(TEXT("Pelvis posterior support floor"),R.Transform.GetLocation().X>=S.Regions[I].Rest.X*S.Regions[I].PelvisAttachment);
                if(Previous.Regions.Num()==5) TestTrue(TEXT("One degree sweep continuous bounded displacement"),(R.Transform.GetLocation()-Previous.Regions[I].Transform.GetLocation()).Size()<1);
            }
            for(int32 FPS:{30,60,120})
            {
                FVamGluteStructuralState Repeat;for(int32 Frame=0;Frame<FPS;++Frame) Repeat=VamGluteStructure::Evaluate(*P,S,T);
                TestTrue(TEXT("Same pose exactly independent of frame count"),Repeat.Regions[0].Transform.Equals(State.Regions[0].Transform,0));
            }
            Previous=State;
        }
        for(double Angle:{-.3,.8}) { auto T=S.RestThighInAnchor;T.SetRotation(FQuat(Direction,Angle));TestTrue(TEXT("Both signs of every hip axis affect structure"),!VamGluteStructure::Evaluate(*P,S,T).Regions[0].Transform.Equals(Neutral.Regions[0].Transform,1.e-5)); }
    }
    auto Mirror=S;Mirror.SideSign=-1;FVector Location=Mirror.RestThighInAnchor.GetLocation();Location.Y=-Location.Y;Mirror.RestThighInAnchor.SetLocation(Location);
    for(auto& R:Mirror.Regions) { R.Rest.Y=-R.Rest.Y;R.PelvisPoint.Y=-R.PelvisPoint.Y;R.ThighPointLocal.Y=-R.ThighPointLocal.Y; }
    for(int32 Axis=0;Axis<3;++Axis)
    {
        FVector A=FVector::ZeroVector;A[Axis]=1;auto L=S.RestThighInAnchor,R=Mirror.RestThighInAnchor;
        L.SetRotation(FQuat(A,.6));R.SetRotation(FQuat(A,Axis==1?.6:-.6));
        const auto LS=VamGluteStructure::Evaluate(*P,S,L),RS=VamGluteStructure::Evaluate(*P,Mirror,R);
        for(int32 I=0;I<5;++I) { FVector Pos=LS.Regions[I].Transform.GetLocation();Pos.Y=-Pos.Y;TestTrue(TEXT("Mirrored pose/rest yields mirrored position"),Pos.Equals(RS.Regions[I].Transform.GetLocation(),1.e-8)); }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVamGluteNativeTest,"Vam.Glute.NativeRuntime",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FVamGluteNativeTest::RunTest(const FString& Parameters)
{
    FString Path;if(!FParse::Value(FCommandLine::Get(),TEXT("VamGluteTestConfig="),Path)) { AddError(TEXT("Pass committed -VamGluteTestConfig path"));return false; }
    auto* C=LoadObject<UVamRuntimeConfiguration>(nullptr,*Path);if(!TestNotNull(TEXT("Reload config"),C)) return false;
    auto* D=C->Definition.LoadSynchronous();auto* P=C->GluteStructure.LoadSynchronous();
    if(!TestNotNull(TEXT("Reload definition"),D) || !TestNotNull(TEXT("Reload profile"),P)) return false;
    TestEqual(TEXT("Persisted G0 hierarchy/weights/zero bind"),UVamGluteStructureBuilder::Validate(D,P),FString());
    const FVector ImmutableRest=P->Sides[0].Regions[0].Rest;
    const double ImmutableVolume=P->Sides[0].EffectiveVolumeCm3;
    TSharedPtr<FJsonObject> Receipt;
    if(FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(C->ReceiptJson),Receipt))
    {
        auto* Source=LoadObject<UVamCharacterDefinition>(nullptr,*Receipt->GetStringField(TEXT("source_definition")));
        FVamNativeMeshInput Original,Derived;FString Error;
        if(TestNotNull(TEXT("Original native source"),Source) && TestTrue(TEXT("Extract original morphs"),UVamBreastJiggleBuilder::ExtractNative(Source->Body.LoadSynchronous(),Original,Error)) && TestTrue(TEXT("Extract derived morphs"),UVamBreastJiggleBuilder::ExtractNative(D->Body.LoadSynchronous(),Derived,Error)))
        {
            TestEqual(TEXT("Morph target count preserved"),Original.Morphs.Num(),Derived.Morphs.Num());
            bool Preserved=Original.Vertices.Num()==Derived.Vertices.Num();
            for(int32 V=0;V<Original.Vertices.Num() && Preserved;++V) Preserved=Original.Vertices[V].Equals(Derived.Vertices[V],1.e-6);
            TestTrue(TEXT("Imported positions preserved"),Preserved);
            for(int32 I=0;I<Original.Bones.Num();++I) TestTrue(TEXT("All source bone indices/binds preserved"),Original.Bones[I].Name==Derived.Bones[I].Name && Original.Bones[I].Parent==Derived.Bones[I].Parent && Original.Bones[I].LocalBind.Equals(Derived.Bones[I].LocalBind,1.e-6));
            for(const auto& M:Original.Morphs)
            {
                const auto* Next=Derived.Morphs.FindByPredicate([&](const FVamBuildMorph& R){return R.Name==M.Name;});
                bool Equal=Next && Next->Deltas.Num()==M.Deltas.Num();
                for(int32 V=0;V<M.Deltas.Num() && Equal;++V) Equal=M.Deltas[V].Equals(Next->Deltas[V],1.e-6);
                TestTrue(TEXT("Every native Morph delta preserved"),Equal);
            }
        }
    }
    for(const auto& S:P->Sides)
    {
        double Fractions=0;for(const auto& R:S.Regions) Fractions+=R.MassFractionCandidate;
        TestTrue(TEXT("Geometry-derived regional fractions partition volume"),FMath::Abs(Fractions-1)<1.e-8);
        for(int32 Axis=0;Axis<3;++Axis) for(int32 Angle=-30;Angle<=120;++Angle)
        {
            FVector V=FVector::ZeroVector;V[Axis]=1;auto T=S.RestThighInAnchor;T.SetRotation(FQuat(V,FMath::DegreesToRadians(double(Angle)))*T.GetRotation());
            const auto State=VamGluteStructure::Evaluate(*P,S,T);
            if(Angle%30==0)
            {
                auto* Mesh=D->Body.Get();const auto& Ref=Mesh->GetRefSkeleton();auto Pose=Ref.GetRefBonePose();
                for(int32 I=0;I<Pose.Num();++I) if(Ref.GetParentIndex(I)>=0) Pose[I]=Pose[I]*Pose[Ref.GetParentIndex(I)];
                const FTransform Anchor=S.AnchorLocal*Pose[S.PelvisBone];
                for(int32 N=0;N<5;++N) Pose[S.Regions[N].BoneIndex]=State.Regions[N].Transform*Anchor;
                bool Bounded=true;
                for(const auto& Section:Mesh->GetImportedModel()->LODModels[0].Sections) for(const auto& Vertex:Section.SoftVertices)
                {
                    FVector Skinned=FVector::ZeroVector;double Sum=0;
                    for(int32 K=0;K<MAX_TOTAL_INFLUENCES;++K) if(Vertex.InfluenceWeights[K])
                    {
                        const int32 Bone=Section.BoneMap[Vertex.InfluenceBones[K]];const double Weight=Vertex.InfluenceWeights[K];Sum+=Weight;
                        Skinned+=Pose[Bone].TransformPosition(FVector(Mesh->GetRefBasesInvMatrix()[Bone].TransformPosition(Vertex.Position)))*Weight;
                    }
                    Skinned/=Sum;Bounded&=!Skinned.ContainsNaN() && (Skinned-FVector(Vertex.Position)).Size()<S.Dimensions.Size()*5;
                }
                TestTrue(TEXT("Actual weighted mesh structural deformation finite and bounded"),Bounded);
            }
            for(const auto& R:State.Regions) TestTrue(TEXT("Actual character supported pose finite and bounded"),!R.Transform.ContainsNaN() && R.Transform.GetLocation().Size()<S.Dimensions.Size()*5+S.COM.Size());
        }
    }
    const auto IVS=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true).RequiresHitProxies(false).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(true).SetTransactional(false);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&IVS);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);World->InitializeActorsForPlay(FURL());World->GetWorldSettings()->NotifyBeginPlay();
    auto* A=World->SpawnActor<AVamCharacterActor>();auto* B=World->SpawnActor<AVamCharacterActor>();
    A->Character->RuntimeConfiguration=C;B->Character->RuntimeConfiguration=C;A->LoadCharacter();B->LoadCharacter();
    auto Tick=[&](double Dt){ ++GFrameCounter;FTSTicker::GetCoreTicker().Tick(Dt);FTickableGameObject::TickObjects(nullptr,LEVELTICK_All,false,Dt);World->Tick(LEVELTICK_All,Dt); };
    for(int32 I=0;I<30;++I) { FlushAsyncLoading();Tick(1./60); }
    auto* MA=Cast<UVamGluteSkeletalMeshComponent>(A->Character->Body);auto* MB=Cast<UVamGluteSkeletalMeshComponent>(B->Character->Body);
    if(TestNotNull(TEXT("Ordinary native character A"),MA) && TestNotNull(TEXT("Ordinary native character B"),MB))
    {
        TestEqual(TEXT("Both G0 sides live"),MA->GluteStates.Num(),2);
        if(MA->GluteStates.Num()==2 && MB->GluteStates.Num()==2)
        {
            const auto Peer=MB->GluteStates;const auto Neutral=MA->GluteStates;
            TestEqual(TEXT("Primary hip snapshot both sides"),MA->HipPoseState.Sides.Num(),2);
            TestTrue(TEXT("Snapshot reads primary pelvis"),MA->HipPoseState.PelvisComponent.Equals(MA->GetComponentSpaceTransforms()[P->Sides[0].PelvisBone],1.e-8));
            MA->DebugGluteSide=0;MA->GlutePoseCommand(TEXT("Hip flexion"));Tick(1./60);
            TestTrue(TEXT("One sided pose keeps other structural state"),MA->GluteStates[1].Regions[0].Transform.Equals(Neutral[1].Regions[0].Transform,1.e-6));
            MA->GlutePoseCommand(TEXT("Reset"));MA->DebugGluteSide=-1;Tick(1./60);
            // Poison only helper outputs: final structural evaluation must use original primary bones.
            auto Expected=MA->GluteStates;
            for(const auto& Side:P->Sides) for(const auto& Region:Side.Regions) MA->GetEditableComponentSpaceTransforms()[Region.BoneIndex].AddToTranslation(FVector(100,200,300));
            MA->FinalizeBoneTransform();
            TestTrue(TEXT("Helper output cannot feed back into structural inputs"),MA->GluteStates[0].Regions[0].Transform.Equals(Expected[0].Regions[0].Transform,1.e-8));
            const auto Anchor=MA->GetComponentSpaceTransforms()[P->Sides[0].AnchorBone];
            for(FName Command:{FName(TEXT("Hip flexion")),FName(TEXT("Hip extension")),FName(TEXT("Abduction")),FName(TEXT("External rotation"))})
            {
                MA->GlutePoseCommand(Command);Tick(1./60);
                TestTrue(TEXT("Final thigh pose changes G0 without actor motion"),!MA->GluteStates[0].Regions[0].Transform.Equals(Neutral[0].Regions[0].Transform,1.e-5));
                TestTrue(TEXT("Thigh pose does not move pelvis anchor"),Anchor.Equals(MA->GetComponentSpaceTransforms()[P->Sides[0].AnchorBone],1.e-6));
            }
            const auto Baseline=MA->GluteStates[0];
            for(double FPS:{30.,60.,120.}) { Tick(1/FPS);TestTrue(TEXT("Native hook frame rate equivalence"),MA->GluteStates[0].Regions[0].Transform.Equals(Baseline.Regions[0].Transform,1.e-6)); }
            TestTrue(TEXT("Peer instance unchanged"),Peer[0].Regions[0].Transform.Equals(MB->GluteStates[0].Regions[0].Transform,1.e-6));
            const auto* Skeleton=MA->GetSkeletalMeshAsset()->GetSkeleton();const int32 Count=MA->GetSkeletalMeshAsset()->GetRefSkeleton().GetRawBoneNum();
            // Select actual supported morph endpoints by measured geometric response, never by preset/name.
            struct FCandidate { FName Parameter;float Value;double Change; };
            TArray<FCandidate> Candidates;
            for(const auto& Response:P->Sides[0].ShapeResponses)
                if(const auto* Param=D->Parameters.FindByPredicate([&](const FVamMorphParameter& R){return R.Target==Response.Parameter;}))
                {
                    const float Value=Response.LogVolume>=0?Param->Maximum:Param->Minimum;
                    const double Change=Response.LogVolume*(Value-Response.DefaultValue);
                    if(Change>0) Candidates.Add({Response.Parameter,Value,Change});
                }
            Candidates.Sort([](const auto& X,const auto& Y){return X.Change>Y.Change;});
            double RequestedLogChange=0;TMap<FName,float> ShapeValues;
            for(const auto& Candidate:Candidates)
            {
                ShapeValues.Add(Candidate.Parameter,Candidate.Value);RequestedLogChange+=Candidate.Change;
                AddInfo(FString::Printf(TEXT("G0_SHAPE_PARAMETER %s=%.6f logVolume=%.6f"),*Candidate.Parameter.ToString(),Candidate.Value,Candidate.Change));
                if(RequestedLogChange>=.25) break;
            }
            TestTrue(TEXT("Source MorphSet provides a materially different glute shape"),RequestedLogChange>=.15);
            auto PriorState=MA->GluteStates;
            for(int32 Step=0;Step<=40;++Step)
            {
                TMap<FName,float> Intermediate;
                for(const auto& Value:ShapeValues)
                {
                    const auto* Param=D->Parameters.FindByPredicate([&](const FVamMorphParameter& R){return R.Target==Value.Key;});
                    Intermediate.Add(Value.Key,FMath::Lerp(Param->DefaultValue,Value.Value,Step/40.f));
                }
                TestTrue(TEXT("Continuous Shape preview accepted"),A->Character->PreviewParameters(Intermediate));Tick(1./60);
                for(int32 Side=0;Side<2;++Side) for(int32 N=0;N<5;++N)
                {
                    const auto& Current=MA->GluteStates[Side].Regions[N];
                    TestFalse(TEXT("Shape sweep finite"),Current.Transform.ContainsNaN());
                    if(Step) TestTrue(TEXT("Shape sweep continuous"),(Current.Transform.GetLocation()-PriorState[Side].Regions[N].Transform.GetLocation()).Size()<2);
                }
                PriorState=MA->GluteStates;
            }
            TestTrue(TEXT("Shape revision snapshot populated"),MA->HipPoseState.ShapeRevision>0);
            TestTrue(TEXT("Large supported Shape preview accepted"),A->Character->PreviewParameters(ShapeValues));
            TestTrue(TEXT("Large Shape commit accepted"),A->Character->CommitShape());Tick(1./60);
            const double ShapeRatio=MA->GluteRest[0].EffectiveVolumeCm3/ImmutableVolume;
            TestTrue(TEXT("Second glute Shape has at least 15 percent different effective volume"),ShapeRatio>=1.15);
            AddInfo(FString::Printf(TEXT("G0_SHAPE_COMPARISON imported %.6f changed %.6f ratio %.6f dimensions %s"),ImmutableVolume,MA->GluteRest[0].EffectiveVolumeCm3,ShapeRatio,*MA->GluteRest[0].Dimensions.ToString()));
            for(FName Command:{FName(TEXT("Hip flexion")),FName(TEXT("Hip extension")),FName(TEXT("Abduction")),FName(TEXT("External rotation"))})
            {
                MA->GlutePoseCommand(Command);Tick(1./60);
                for(const auto& Side:MA->GluteStates) for(const auto& Region:Side.Regions) TestFalse(TEXT("Large Shape all pose modes finite"),Region.Transform.ContainsNaN());
            }
            TestTrue(TEXT("Shape edit leaves other instance geometry unchanged"),FMath::IsNearlyEqual(MB->GluteRest[0].EffectiveVolumeCm3,ImmutableVolume,1.e-6));
            TestTrue(TEXT("Shape preserves helper identity"),Skeleton==MA->GetSkeletalMeshAsset()->GetSkeleton() && Count==MA->GetSkeletalMeshAsset()->GetRefSkeleton().GetRawBoneNum());
            TestTrue(TEXT("Profile remains immutable"),P->Sides[0].Regions[0].Rest==ImmutableRest && P->Sides[0].EffectiveVolumeCm3==ImmutableVolume);
            AddInfo(MA->GluteDiagnostics());MA->GlutePoseCommand(TEXT("Reset"));
        }
    }
    A->Destroy();B->Destroy();Tick(1./60);World->BeginTearingDown();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);return true;
}
#endif

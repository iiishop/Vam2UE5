#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "VamGluteStructure.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVamGluteRefinementTest,"Vam.Glute.PoseRefinement",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FVamGluteRefinementTest::RunTest(const FString& Parameters)
{
    auto* P=NewObject<UVamGluteStructureProfile>();P->SchemaVersion=2;P->RefinementVersion=1;
    FVamGluteSide S;S.Side=TEXT("Left");S.Dimensions=FVector(15,19,24);S.RestThighInAnchor=FTransform(FVector(0,8,-2));
    const TCHAR* Names[]={TEXT("Core"),TEXT("Upper"),TEXT("Lower"),TEXT("Medial"),TEXT("Lateral")};
    for(int32 I=0;I<5;++I)
    {
        FVamGluteRegion R;R.Semantic=Names[I];R.Rest=FVector(9,8+(I-2),-3+(I-2)*2);R.MassCenter=R.Rest;
        R.PelvisPoint=FVector(0,R.Rest.Y,R.Rest.Z);R.ThighPointLocal=FVector(2,1,-5);
        R.PelvisAttachment=.7-I*.05;R.ThighAttachment=1-R.PelvisAttachment;R.SupportBaseline=10;R.MassFractionCandidate=.2;S.Regions.Add(R);
    }
    VamGluteStructure::CalibratePoseRefinement(S);
    const auto Neutral=VamGluteStructure::Evaluate(*P,S,S.RestThighInAnchor);
    for(int32 I=0;I<5;++I) TestTrue(TEXT("G05 neutral bind identity"),Neutral.Regions[I].Transform.Equals(FTransform(VamGluteStructure::FiberBasis(S,S.Regions[I]),S.Regions[I].Rest),1.e-9));
    auto Flex=S.RestThighInAnchor;Flex.SetRotation(FQuat(FVector::YAxisVector,1.2));
    auto Ext=S.RestThighInAnchor;Ext.SetRotation(FQuat(FVector::YAxisVector,-.4));
    const auto FS=VamGluteStructure::Evaluate(*P,S,Flex),ES=VamGluteStructure::Evaluate(*P,S,Ext);
    TestTrue(TEXT("Hip flexion decomposition"),FMath::Abs(FS.HipPose.FlexionExtension-1.2)<1.e-9);
    TestTrue(TEXT("Core and upper support rise on flexion"),FS.Regions[0].Support>Neutral.Regions[0].Support && FS.Regions[1].Support>Neutral.Regions[1].Support);
    TestTrue(TEXT("Fold extension activation"),ES.FoldState.MiddleTransitionFactor>0);
    TestTrue(TEXT("Fold flexion stretch"),FS.FoldState.StretchState.Y>0);
    TestTrue(TEXT("Distinct regional response"),!FS.Regions[0].StructuralOffset.Equals(FS.Regions[2].StructuralOffset,1.e-5));
    auto Mirror=S;Mirror.Side=TEXT("Right");Mirror.SideSign=-1;
    FVector Loc=Mirror.RestThighInAnchor.GetLocation();Loc.Y=-Loc.Y;Mirror.RestThighInAnchor.SetLocation(Loc);
    for(auto& R:Mirror.Regions) { R.Rest.Y*=-1;R.MassCenter.Y*=-1;R.PelvisPoint.Y*=-1;R.ThighPointLocal.Y*=-1; }
    VamGluteStructure::CalibratePoseRefinement(Mirror);
    for(int32 Axis=0;Axis<3;++Axis)
    {
        FVector A=FVector::ZeroVector;A[Axis]=1;FVamGluteStructuralState Previous;
        for(int32 Degrees=-45;Degrees<=130;++Degrees)
        {
            auto T=S.RestThighInAnchor;T.SetRotation(FQuat(A,FMath::DegreesToRadians(double(Degrees))));
            auto MT=Mirror.RestThighInAnchor;MT.SetRotation(FQuat(A,FMath::DegreesToRadians(double(Degrees)*(Axis==1?1:-1))));
            const auto State=VamGluteStructure::Evaluate(*P,S,T),MS=VamGluteStructure::Evaluate(*P,Mirror,MT);
            for(int32 I=0;I<5;++I)
            {
                const auto& N=State.Regions[I];FVector Mirrored=N.Transform.GetLocation();Mirrored.Y*=-1;
                TestTrue(TEXT("Mirror positions"),Mirrored.Equals(MS.Regions[I].Transform.GetLocation(),1.e-8));
                TestFalse(TEXT("Finite structural transform"),N.Transform.ContainsNaN());
                TestTrue(TEXT("Positive finite normalized attachments"),FMath::IsFinite(N.Support) && N.Support>0 && FMath::Abs(N.PelvisAttachment+N.ThighAttachment-1)<1.e-12);
                TestTrue(TEXT("Bounded node movement"),N.StructuralOffset.Size()<S.Dimensions.Size()*.3);
                TestTrue(TEXT("Scaffold determinant one"),FMath::Abs(N.Transform.GetScale3D().X*N.Transform.GetScale3D().Y*N.Transform.GetScale3D().Z-1)<1.e-12);
                TestTrue(TEXT("Posterior support retained"),N.Transform.GetLocation().X>=S.Regions[I].Rest.X*S.Regions[I].PelvisAttachment);
                if(Previous.Regions.Num()==5) TestTrue(TEXT("One degree continuity"),(N.Transform.GetLocation()-Previous.Regions[I].Transform.GetLocation()).Size()<1);
                for(int32 FPS:{30,60,120}) { FVamGluteStructuralState Repeated;for(int32 Frame=0;Frame<FPS;++Frame) Repeated=VamGluteStructure::Evaluate(*P,S,T);TestTrue(TEXT("Exact FPS equivalence"),Repeated.Regions[I].Transform.Equals(N.Transform,0)); }
            }
            Previous=State;
        }
    }
    FVamGluteStructuralState Previous;
    for(int32 Step=0;Step<=600;++Step)
    {
        const double T=Step*.01;auto Pose=S.RestThighInAnchor;
        Pose.SetRotation(FQuat(FVector::YAxisVector,1.1*FMath::Sin(T))*FQuat(FVector::XAxisVector,.6*FMath::Cos(T))*FQuat(S.FemurAxisInAnchor,.5*FMath::Sin(T*2)));
        const auto State=VamGluteStructure::Evaluate(*P,S,Pose);
        for(int32 I=0;I<5;++I) { TestFalse(TEXT("Combined pose finite"),State.Regions[I].Transform.ContainsNaN());if(Step) TestTrue(TEXT("Combined pose continuous"),(State.Regions[I].Transform.GetLocation()-Previous.Regions[I].Transform.GetLocation()).Size()<1); }
        Previous=State;
    }
    TArray<FVamGluteSide> Sides={S,Mirror};const FTransform Pelvis(FQuat(FVector::YAxisVector,.3),FVector(3,4,5));
    auto Snapshot=VamGluteStructure::CaptureHipPose(*P,Sides,Pelvis,Flex*Pelvis,Mirror.RestThighInAnchor*Pelvis,7);
    TestTrue(TEXT("Primary relative pose independent of pelvis transform"),FMath::Abs(Snapshot.Sides[0].FlexionExtension-1.2)<1.e-9);
    TestTrue(TEXT("Other side independent"),FMath::Abs(Snapshot.Sides[1].FlexionExtension)<1.e-9);
    TestTrue(TEXT("Pelvis tilt diagnostic"),FMath::Abs(Snapshot.PelvisTilt-.3)<1.e-9);
    TArray<uint8> Bytes;FMemoryWriter Writer(Bytes);FVamHipPoseState::StaticStruct()->SerializeItem(Writer,&Snapshot,nullptr);
    FVamHipPoseState Loaded;FMemoryReader Reader(Bytes);FVamHipPoseState::StaticStruct()->SerializeItem(Reader,&Loaded,nullptr);
    TestEqual(TEXT("Hip snapshot revision serializes"),Loaded.ShapeRevision,7);
    TestTrue(TEXT("Hip relative transform serializes"),Loaded.Sides.Num()==2 && Loaded.Sides[0].FemurInAnchor.Equals(Snapshot.Sides[0].FemurInAnchor,0));
    return true;
}
#endif

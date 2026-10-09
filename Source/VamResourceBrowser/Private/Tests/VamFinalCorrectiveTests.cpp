#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "VamRuntimeConfiguration.h"
#include "VamCharacterDefinition.h"
#include "VamGluteCorrectiveProfile.h"
#include "VamGluteStructureProfile.h"
#include "VamLegJiggleProfile.h"
#include "VamBreastJiggleProfile.h"
#include "VamGluteStructureBuilder.h"
#include "Engine/SkeletalMesh.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/SkinWeightVertexBuffer.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVamFinalCorrectiveTest,"Vam.Glute.FinalCorrective",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FVamFinalCorrectiveTest::RunTest(const FString&)
{
    FString Path;if(!FParse::Value(FCommandLine::Get(),TEXT("VamFinalCorrectiveConfig="),Path)){AddError(TEXT("Explicit final configuration required"));return false;}
    auto* C=LoadObject<UVamRuntimeConfiguration>(nullptr,*Path);if(!TestNotNull(TEXT("Final config"),C))return false;
    auto* D=C->Definition.LoadSynchronous();auto* P=C->GluteCorrective.LoadSynchronous();auto* G=C->GluteStructure.LoadSynchronous();auto* L=C->LegJiggle.LoadSynchronous();auto* B=C->BreastJiggle.LoadSynchronous();
    const FString Replay=UVamGluteStructureBuilder::ValidateCorrective(D,G,L,B,P);TestTrue(TEXT("Final authored-pose replay and data seal"),Replay.IsEmpty());if(!Replay.IsEmpty()){AddError(Replay);return false;}
    auto* M=D->Body.LoadSynchronous();auto& W=M->GetResourceForRendering()->LODRenderData[0].SkinWeightVertexBuffer;const uint16 Before=W.GetBoneWeight(0,0);W.SetBoneWeight(0,0,Before==65535?Before-1:Before+1);
    const FString WeightFailure=P->ValidateFinalData(D,G,L,B);W.SetBoneWeight(0,0,Before);TestTrue(TEXT("One quantized weight unit invalidates old corrective"),WeightFailure.Contains(TEXT("final_weights")));
    const double Density=G->DensityCandidateKgPerCm3;G->DensityCandidateKgPerCm3*=1.01;const FString HelperFailure=P->ValidateFinalData(D,G,L,B);G->DensityCandidateKgPerCm3=Density;TestTrue(TEXT("Helper data mutation invalidates corrective"),HelperFailure.Contains(TEXT("helper_definitions")));
    TestTrue(TEXT("Restored data matches"),P->ValidateFinalData(D,G,L,B).IsEmpty());
    FString Report;if(FParse::Value(FCommandLine::Get(),TEXT("VamFinalCorrectiveReport="),Report))FFileHelper::SaveStringToFile(P->FinalReplayJson,*Report);
    AddInfo(WeightFailure);AddInfo(HelperFailure);return !HasAnyErrors();
}
#endif

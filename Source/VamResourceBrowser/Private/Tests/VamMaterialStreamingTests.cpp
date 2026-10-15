#include "CoreMinimal.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "VamStage06AssetEditor.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionConstant2Vector.h"
#include "Materials/MaterialExpressionAdd.h"
#include "Materials/MaterialExpressionDivide.h"
#include "Materials/MaterialExpressionWorldPosition.h"
#include "Engine/Texture2D.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVamMaterialStreamingTest,"Vam.Material.AffineStreaming",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FVamMaterialStreamingTest::RunTest(const FString&)
{
 auto* M=NewObject<UMaterial>();auto* T=NewObject<UTexture2D>(M);auto* UV=NewObject<UMaterialExpressionTextureCoordinate>(M);UV->CoordinateIndex=1;UV->UTiling=2;UV->VTiling=4;
 auto* Offset=NewObject<UMaterialExpressionConstant2Vector>(M);Offset->R=.2f;Offset->G=-.3f;
 auto* Add=NewObject<UMaterialExpressionAdd>(M);Add->A.Expression=UV;Add->A.Mask=1;Add->A.MaskR=1;Add->A.MaskG=1;Add->B.Expression=Offset;
 auto* Sample=NewObject<UMaterialExpressionTextureSample>(M);Sample->Texture=T;Sample->Coordinates.Expression=Add;
 M->GetExpressionCollection().AddExpression(UV);M->GetExpressionCollection().AddExpression(Offset);M->GetExpressionCollection().AddExpression(Add);M->GetExpressionCollection().AddExpression(Sample);
 TestTrue(TEXT("Affine coordinates build without a render device"),UVamStage06AssetEditor::BuildAffineMaterialStreaming(M).IsEmpty());
 const auto Before=M->GetTextureStreamingData();if(!TestEqual(TEXT("One texture footprint"),Before.Num(),1))return false;
 TestEqual(TEXT("Actual UV channel retained"),Before[0].UVChannelIndex,1);TestEqual(TEXT("Conservative anisotropic scale"),Before[0].SamplingScale,2.f);TestEqual(TEXT("Texture identity retained"),Before[0].TextureName,T->GetFName());
 Sample->Coordinates.Expression=NewObject<UMaterialExpressionWorldPosition>(M);
 TestFalse(TEXT("Unknown coordinate graph is rejected, not assumed UV0"),UVamStage06AssetEditor::BuildAffineMaterialStreaming(M).IsEmpty());
 TestEqual(TEXT("Failed build does not partially overwrite existing data"),M->GetTextureStreamingData()[0].SamplingScale,Before[0].SamplingScale);
 Sample->Coordinates.Expression=Add;UV->UTiling=UV->VTiling=0;
 TestFalse(TEXT("Degenerate footprint rejected"),UVamStage06AssetEditor::BuildAffineMaterialStreaming(M).IsEmpty());
 auto* Div=NewObject<UMaterialExpressionDivide>(M);Div->A.Expression=UV;Div->ConstB=2;UV->UTiling=UV->VTiling=4;Sample->Coordinates.Expression=Div;
 TestTrue(TEXT("Constant division is affine"),UVamStage06AssetEditor::BuildAffineMaterialStreaming(M).IsEmpty());
 TestEqual(TEXT("Division footprint"),M->GetTextureStreamingData()[0].SamplingScale,2.f);
 Div->ConstB=0;TestFalse(TEXT("Zero divisor rejected"),UVamStage06AssetEditor::BuildAffineMaterialStreaming(M).IsEmpty());
 return !HasAnyErrors();
}
#endif

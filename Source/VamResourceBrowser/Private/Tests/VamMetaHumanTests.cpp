#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "VamMetaHumanComponent.h"
#include "VamMetaHumanEditorAdapter.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVamMHRevisionTest,"Vam.MetaHuman.RevisionIsolation",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FVamMHRevisionTest::RunTest(const FString&)
{
    auto* A=NewObject<UVamMetaHumanComponent>();auto* B=NewObject<UVamMetaHumanComponent>();
    A->NotifySurfaceChanged();
    TestEqual(TEXT("Surface change remains local"),A->SurfaceRevision,1);
    TestEqual(TEXT("Surface does not invalidate shape"),A->ShapeRevision,0);
    TestEqual(TEXT("Surface does not invalidate equipment"),A->EquipmentRevision,0);
    A->NotifyEquipmentChanged(); A->NotifyShapeChanged();
    TestEqual(TEXT("Other instance surface untouched"),B->SurfaceRevision,0);
    TestEqual(TEXT("Other instance shape untouched"),B->ShapeRevision,0);
    TestEqual(TEXT("Other instance equipment untouched"),B->EquipmentRevision,0);
    TestFalse(TEXT("Null is never full rig"),UVamMetaHumanEditorAdapter::HasFullRig(nullptr));
    TestFalse(TEXT("Null is never character source"),UVamMetaHumanEditorAdapter::IsCharacterSourceValid(nullptr));
    FString Error;
    TestNull(TEXT("Bad target geometry cannot create asset"),UVamMetaHumanEditorAdapter::CreateTarget(TEXT("/Game/MH00InvalidTarget"),{FVector::ZeroVector},{0,1,2},Error));
    return true;
}
#endif

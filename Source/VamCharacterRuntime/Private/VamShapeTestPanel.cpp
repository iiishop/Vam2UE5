#include "VamShapeTestPanel.h"
#include "VamCharacterActor.h"
#include "VamCharacterComponent.h"
#include "VamCharacterDefinition.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Engine/Canvas.h"
#include "Components/SkeletalMeshComponent.h"
#include "SkeletalRenderPublic.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Engine/GameViewportClient.h"
#if WITH_EDITOR
#include "ShaderCompiler.h"
#endif

AVamShapeTestGameMode::AVamShapeTestGameMode() { HUDClass=AVamShapeTestPanel::StaticClass(); DefaultPawnClass=nullptr; PrimaryActorTick.bCanEverTick=true; }

void AVamShapeTestGameMode::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (TestPhase>=12 || !FParse::Param(FCommandLine::Get(),TEXT("VamShapeSelfTest"))) return;
    const double Now=FPlatformTime::Seconds();
    if (TestTime==0) TestTime=Now;
#if WITH_EDITOR
    if (GShaderCompilingManager && GShaderCompilingManager->IsCompiling()) { TestTime=Now; return; }
#endif
    if (Now-TestTime<3) return;
    TArray<UVamCharacterComponent*> Characters;
    for (TActorIterator<AVamCharacterActor> It(GetWorld());It;++It) if (It->Character->Body) Characters.Add(It->Character);
    Characters.Sort([](const UVamCharacterComponent& A,const UVamCharacterComponent& B){return A.GetOwner()->GetName()<B.GetOwner()->GetName();});
    if (Characters.Num()!=2) return;
    // Asset readiness does not imply a rendered frame: PSO precaching can
    // postpone the first draw. Start the screenshot warm-up after both loads.
    if (!bSelfTestReady) { bSelfTestReady=true; TestTime=Now; return; }
    if (TestPhase==0 && Now-TestTime<10) return;
    auto* A=Characters[0];auto* B=Characters[1];
#if WITH_EDITOR
    // Keep the validation renderer in CPU mode. Repeated mode switches rebuild
    // render resources and can invalidate the pose being measured.
    if (!A->Body->GetCPUSkinningEnabled() || !B->Body->GetCPUSkinningEnabled())
    {
        A->Body->SetCPUSkinningEnabled(true,true);B->Body->SetCPUSkinningEnabled(true,true);
        TestTime=Now;return;
    }
#endif
    auto Capture=[](USkeletalMeshComponent* Body)
    {
#if WITH_EDITOR
        // Acceptance only: synchronous CPU read, never part of the normal shape path.
        TArray<FFinalSkinVertex> Vertices; Body->GetCPUSkinnedCachedFinalVertices(Vertices);
        TArray<FVector3f> Positions; for (const auto& V:Vertices) Positions.Add(V.Position); return Positions;
#else
        // Cook strips raw Morph deltas. GPU appearance is checked in screenshots.
        TArray<FVector3f> Positions; for (const auto& T:Body->GetComponentSpaceTransforms()) Positions.Add(FVector3f(T.GetTranslation())); return Positions;
#endif
    };
    auto Error=[](const TArray<FVector3f>& A,const TArray<FVector3f>& B)
    {
        if (A.IsEmpty() || A.Num()!=B.Num()) return -1.f;
        float Maximum=0;for (int32 I=0;I<A.Num();++I) Maximum=FMath::Max(Maximum,(A[I]-B[I]).Size());return Maximum;
    };
    bool Passed=true;
    if (TestPhase==0)
    {
        InitialVertices=Capture(A->Body);OtherVertices=Capture(B->Body);
        FScreenshotRequest::RequestScreenshot(TEXT("Stage05_Imported.png"),true,false);
    }
    else if (TestPhase==1) A->ResetToBaseShape();
    else if (TestPhase==3) A->CancelShape();
    else if (TestPhase==5) { A->ResetToBaseShape();A->CommitShape();A->ResetToImportedAppearance();A->CancelShape(); }
    else if (TestPhase==7) { A->ResetToImportedAppearance();A->CommitShape(); }
    else if (TestPhase==8)
    {
        FScreenshotRequest::RequestScreenshot(TEXT("Stage05_Restored.png"),true,false);
    }
    else if (TestPhase==9)
    {
        for (const auto& P:A->Definition.Get()->Parameters) if (P.DefaultValue==0 && P.Maximum>0) A->SetParameter(P.Target,FMath::Min(.5f,P.Maximum));
    }
    else if (TestPhase==11)
    {
        A->ResetToImportedAppearance();A->CommitShape();
        UE_LOG(LogTemp,Display,TEXT("VAM_SHAPE_COOKED_OK: shape pose, cancel, commit, reset, editable zero controls, two-instance isolation; inspect screenshots for GPU morphology"));
        FPlatformMisc::RequestExit(false);
    }
    else
    {
        const auto Current=Capture(A->Body);const auto Other=Capture(B->Body);
        const float OtherError=Error(OtherVertices,Other),ShapeError=Error(InitialVertices,Current);
        for (const auto& P:A->Definition.Get()->Parameters)
            UE_LOG(LogTemp,Display,TEXT("VAM_SHAPE_VALUE phase=%d target=%s absolute=%f committed=%f curve=%f"),TestPhase,*P.Target.ToString(),A->GetShapeState().Values.FindRef(P.Target),A->GetShapeState(true).Values.FindRef(P.Target),A->Body->GetMorphTarget(P.Target));
        Passed=OtherError>=0 && OtherError<.001;
        if (TestPhase==2 || TestPhase==6) Passed &= ShapeError>.1;
        else if (TestPhase==4) Passed &= ShapeError>=0 && ShapeError<.001;
        else if (TestPhase==10)
        {
#if WITH_EDITOR
            Passed &= ShapeError>.01;
#endif
            for (const auto& P:A->Definition.Get()->Parameters) if (P.DefaultValue==0 && P.Maximum>0)
                Passed &= FMath::IsNearlyEqual(A->Body->GetMorphTarget(P.Target),FMath::Min(.5f,P.Maximum));
            FScreenshotRequest::RequestScreenshot(TEXT("Stage05_Editable.png"),true,false);
        }
        if (TestPhase==2) FScreenshotRequest::RequestScreenshot(TEXT("Stage05_Base.png"),true,false);
        UE_LOG(LogTemp,Display,TEXT("VAM_SHAPE_COOKED_PHASE=%d Passed=%d ShapeDeltaCm=%f OtherDeltaCm=%f Samples=%d CPUVertexProbe=%d"),TestPhase,Passed,ShapeError,OtherError,Current.Num(),WITH_EDITOR);
    }
    if (!Passed) { UE_LOG(LogTemp,Error,TEXT("VAM_SHAPE_COOKED_FAILED"));TestPhase=12;FPlatformMisc::RequestExit(false);return; }
    ++TestPhase;TestTime=Now;
}

void AVamShapeTestPanel::DrawHUD()
{
    Super::DrawHUD();
    if (!Canvas || !PlayerOwner) return;
    PlayerOwner->bShowMouseCursor=true;
    PlayerOwner->bEnableClickEvents=true;
    TArray<UVamCharacterComponent*> Characters;
    for (TActorIterator<AVamCharacterActor> It(GetWorld()); It; ++It) if (It->Character->Body) Characters.Add(It->Character);
    Characters.Sort([](const UVamCharacterComponent& A,const UVamCharacterComponent& B){return A.GetOwner()->GetName()<B.GetOwner()->GetName();});
    if (Characters.IsEmpty()) { DrawText(TEXT("Waiting for native character..."),FLinearColor::White,20,20); return; }
    Selected=Characters[SelectedInstance%Characters.Num()];
    auto* Definition=Selected->Definition.Get(); if (!Definition) return;
    const auto State=Selected->GetShapeState();
    DrawRect(FLinearColor(0.015,0.02,0.03,.92),8,8,410,110+32*Definition->Parameters.Num());
    DrawText(FString::Printf(TEXT("Shape | instance %d/%d | revision %d"),SelectedInstance%Characters.Num()+1,Characters.Num(),State.Revision),FLinearColor::White,18,16);
    Rows.Reset();
    for (int32 i=0;i<Definition->Parameters.Num();++i)
    {
        const auto& P=Definition->Parameters[i]; Rows.Add(P.Target);
        const float V=State.Values.FindRef(P.Target);const float Y=44+i*32;
        DrawText(P.DisplayName.Left(32),FLinearColor::White,18,Y);
        DrawRect(FLinearColor(.15,.18,.22,1),250,Y+4,125,12);
        const float Alpha=P.Maximum>P.Minimum ? (V-P.Minimum)/(P.Maximum-P.Minimum) : 0;
        DrawRect(FLinearColor(.2,.7,.9,1),250,Y+4,125*Alpha,12);
        DrawText(FString::Printf(TEXT("%.2f"),V),FLinearColor::White,378,Y);
        AddHitBox(FVector2D(250,Y),FVector2D(125,24),FName(*FString::Printf(TEXT("Value_%d"),i)),true);
    }
    const float Y=50+Rows.Num()*32;
    const TCHAR* Buttons[]={TEXT("Commit"),TEXT("Cancel"),TEXT("Base"),TEXT("Imported"),TEXT("Next")};
    for (int32 i=0;i<5;++i)
    {
        DrawRect(FLinearColor(.1,.2,.3,1),18+i*78,Y,72,26);
        DrawText(Buttons[i],FLinearColor::White,22+i*78,Y+4);
        AddHitBox(FVector2D(18+i*78,Y),FVector2D(72,26),FName(Buttons[i]),true);
    }
    DrawText(TEXT("Preview edits; Commit publishes Shape, Cancel restores commit."),FLinearColor(.7,.8,.9,1),18,Y+34);
}

void AVamShapeTestPanel::NotifyHitBoxClick(FName Name)
{
    if (!Selected) return;
    if (Name==TEXT("Next")) { ++SelectedInstance; return; }
    if (Name==TEXT("Commit")) Selected->CommitShape();
    else if (Name==TEXT("Cancel")) Selected->CancelShape();
    else if (Name==TEXT("Base")) Selected->ResetToBaseShape();
    else if (Name==TEXT("Imported")) Selected->ResetToImportedAppearance();
    else if (Name.ToString().StartsWith(TEXT("Value_")))
    {
        int32 Index=FCString::Atoi(*Name.ToString().Mid(6));
        auto* D=Selected->Definition.Get();float X,Y;
        if (D && D->Parameters.IsValidIndex(Index) && PlayerOwner->GetMousePosition(X,Y))
        {
            const auto& P=D->Parameters[Index];
            Selected->SetParameter(P.Target,FMath::Lerp(P.Minimum,P.Maximum,FMath::Clamp((X-250)/125,0.f,1.f)));
        }
    }
}


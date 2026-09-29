#pragma once
TSharedRef<SWidget> LegJiggleControls()
{
    auto Body=[]()->UVamLegSkeletalMeshComponent*{auto* A=CurrentActor();return A && A->Character?Cast<UVamLegSkeletalMeshComponent>(A->Character->Body):nullptr;};
    auto Box=SNew(SVerticalBox);
    const TCHAR* Flags[]={TEXT("Enabled"),TEXT("Show Leg Nodes"),TEXT("Show Leg Region (reference samples)"),TEXT("Show Passive Tension")};
    auto Flag=[](UVamLegSkeletalMeshComponent* B,int32 I)->bool&{return I==0?B->bLegJiggleEnabled:I==1?B->bShowLegNodes:I==2?B->bShowLegRegion:B->bShowLegTension;};
    for(int32 I=0;I<4;++I) Box->AddSlot().AutoHeight()[SNew(SCheckBox).IsChecked_Lambda([Body,Flag,I](){auto* B=Body();return B && Flag(B,I)?ECheckBoxState::Checked:ECheckBoxState::Unchecked;}).OnCheckStateChanged_Lambda([Body,Flag,I](ECheckBoxState S){if(auto* B=Body()) Flag(B,I)=S==ECheckBoxState::Checked;})[SNew(STextBlock).Text(FText::FromString(Flags[I]))]];
    const TCHAR* Labels[]={TEXT("Thigh Amplitude · 大腿幅度"),TEXT("Calf Amplitude · 小腿幅度"),TEXT("Support · 支承倍率"),TEXT("Damping · 阻尼倍率")};
    for(int32 I=0;I<4;++I)
    {
        auto Value=[I](UVamLegSkeletalMeshComponent* B)->double&{return I==0?B->ThighAmplitude:I==1?B->CalfAmplitude:I==2?B->LegSupport:B->LegDamping;};
        Box->AddSlot().AutoHeight()[SNew(SHorizontalBox)+SHorizontalBox::Slot().FillWidth(.65)[SNew(STextBlock).Text(FText::FromString(Labels[I]))]+SHorizontalBox::Slot().FillWidth(.35)[SNew(SNumericEntryBox<double>).MinValue(I<2?0.:.1).MaxValue(I==3?4.:10.).Value_Lambda([Body,Value]()->TOptional<double>{auto* B=Body();return B?Value(B):1.;}).OnValueChanged_Lambda([Body,Value](double X){if(auto* B=Body()) Value(B)=X;})]];
    }
    auto Poses=SNew(SWrapBox).UseAllottedSize(true);
    for(const TCHAR* Name:{TEXT("Neutral"),TEXT("Hip Flexion"),TEXT("Knee Flexion"),TEXT("Crouch"),TEXT("Dorsiflexion"),TEXT("Plantarflexion"),TEXT("Reset")}){const FName Command(Name);Poses->AddSlot().Padding(2)[SNew(SButton).Text(FText::FromName(Command)).OnClicked_Lambda([Body,Command](){if(auto* B=Body()) B->LegPoseCommand(Command);return FReply::Handled();})];}
    Box->AddSlot().AutoHeight()[Poses];auto Motion=SNew(SWrapBox).UseAllottedSize(true);
    for(const TCHAR* Name:{TEXT("Smooth Forward Accelerate"),TEXT("Smooth Stop"),TEXT("Jump"),TEXT("Walk Cycle / Alternating Thigh Swing")}){const FName Command(Name);Motion->AddSlot().Padding(2)[SNew(SButton).Text(FText::FromName(Command)).OnClicked_Lambda([Body,Command](){if(auto* B=Body()) B->GluteMotionCommand(Command);return FReply::Handled();})];}
    Box->AddSlot().AutoHeight()[Motion];Box->AddSlot().AutoHeight()[SNew(STextBlock).AutoWrapText(true).Text_Lambda([Body](){auto* B=Body();return FText::FromString(B?B->LegDiagnostics():TEXT("Select a runtime character"));})];
    return SNew(SExpandableArea).InitiallyCollapsed(true).HeaderContent()[SNew(STextBlock).Text(FText::FromString(TEXT("Leg Jiggle · 大腿 / 小腿")))].BodyContent()[Box];
}

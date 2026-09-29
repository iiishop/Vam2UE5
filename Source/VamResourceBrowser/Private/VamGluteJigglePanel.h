#pragma once
// Included inside the existing panel implementation; reuses its actor selection.
TSharedRef<SWidget> GluteJiggleControls()
{
    auto Body=[]()->UVamGluteSkeletalMeshComponent* { auto* A=CurrentActor();return A && A->Character?Cast<UVamGluteSkeletalMeshComponent>(A->Character->Body):nullptr; };
    auto Box=SNew(SVerticalBox);
    auto Flag=[](UVamGluteSkeletalMeshComponent* B,int32 I)->bool& { return I==0?B->bGluteJiggleEnabled:I==1?B->bShowGluteDynamicNodes:I==2?B->bShowGluteDynamicPelvis:I==3?B->bShowGluteDynamicThigh:I==4?B->bShowGluteVelocity:B->bShowGluteRestDynamic; };
    const TCHAR* Names[]={TEXT("Enabled"),TEXT("Show Dynamic Nodes"),TEXT("Show Pelvis Attachment"),TEXT("Show Thigh Attachment"),TEXT("Show Velocity"),TEXT("Show Rest vs Dynamic")};
    for(int32 I=0;I<6;++I) Box->AddSlot().AutoHeight()[SNew(SCheckBox).IsChecked_Lambda([Body,Flag,I](){auto* B=Body();return B && Flag(B,I)?ECheckBoxState::Checked:ECheckBoxState::Unchecked;}).OnCheckStateChanged_Lambda([Body,Flag,I](ECheckBoxState S){if(auto* B=Body()) Flag(B,I)=S==ECheckBoxState::Checked;})[SNew(STextBlock).Text(FText::FromString(Names[I]))]];
    auto Advanced=SNew(SVerticalBox);
    const TCHAR* Labels[]={TEXT("Support · 双支承刚度"),TEXT("Damping · 能量衰减"),TEXT("Mobility · 非线性行程"),TEXT("Internal Coupling · 区域连接"),TEXT("Mass Scale · 质量")};
    const double Minimum[]={.1,.1,.25,0,.1},Maximum[]={10,4,3,4,10};
    for(int32 I=0;I<5;++I)
    {
        auto Value=[I](UVamGluteSkeletalMeshComponent* B)->double& {return I==0?B->GluteSupport:I==1?B->GluteDamping:I==2?B->GluteMobility:I==3?B->GluteInternalCoupling:B->GluteMassScale;};
        auto Row=SNew(SHorizontalBox)+SHorizontalBox::Slot().FillWidth(.65)[SNew(STextBlock).Text(FText::FromString(Labels[I]))]+SHorizontalBox::Slot().FillWidth(.35)[SNew(SNumericEntryBox<double>).MinValue(Minimum[I]).MaxValue(Maximum[I]).MinSliderValue(Minimum[I]).MaxSliderValue(Maximum[I]).Value_Lambda([Body,Value]()->TOptional<double>{auto* B=Body();return B?Value(B):1.;}).OnValueChanged_Lambda([Body,Value](double X){if(auto* B=Body()) Value(B)=X;})];
        (I==4?Advanced:Box)->AddSlot().AutoHeight()[Row];
    }
    Box->AddSlot().AutoHeight()[SNew(SExpandableArea).InitiallyCollapsed(true).HeaderContent()[SNew(STextBlock).Text(FText::FromString(TEXT("Advanced")))].BodyContent()[Advanced]];
    auto Motion=SNew(SWrapBox).UseAllottedSize(true);
    for(const TCHAR* Name:{TEXT("Smooth Forward Accelerate"),TEXT("Smooth Stop"),TEXT("Lateral Accelerate"),TEXT("Smooth Turn"),TEXT("Continuous Turn"),TEXT("Smooth Turn Stop"),TEXT("Jump"),TEXT("Walk Cycle / Alternating Thigh Swing"),TEXT("Hard Stop"),TEXT("Reset")})
    {const FName C(Name);Motion->AddSlot().Padding(2)[SNew(SButton).Text(FText::FromName(C)).OnClicked_Lambda([Body,C](){if(auto* B=Body()) B->GluteMotionCommand(C);return FReply::Handled();})];}
    Box->AddSlot().AutoHeight()[Motion];auto Snapshot=MakeShared<FString>();
    Box->AddSlot().AutoHeight()[SNew(SButton).Text(FText::FromString(TEXT("对比 G1 OFF / 当前动态表面"))).OnClicked_Lambda([Body,Snapshot](){if(auto* B=Body()) *Snapshot=CaptureGluteSurface(*B,true,true);return FReply::Handled();})];
    Box->AddSlot().AutoHeight()[SNew(STextBlock).Text_Lambda([Snapshot](){return FText::FromString(*Snapshot);}).AutoWrapText(true)];
    Box->AddSlot().AutoHeight()[SNew(STextBlock).Text_Lambda([Body](){auto* B=Body();return FText::FromString(B?B->GluteJiggleDiagnostics():TEXT("Select a runtime character"));}).AutoWrapText(true)];
    return SNew(SExpandableArea).InitiallyCollapsed(true).HeaderContent()[SNew(STextBlock).Text(FText::FromString(TEXT("Glute Jiggle - G1")))].BodyContent()[Box];
}

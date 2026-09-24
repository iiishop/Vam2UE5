#include "VamDebugPanel.h"
#include "VamCharacterActor.h"
#include "VamCharacterComponent.h"
#include "VamCharacterDefinition.h"
#include "VamMotionComponent.h"
#include "VamInteractionComponent.h"
#include "VamActivePoseComponent.h"
#include "ComponentVisualizer.h"
#include "Components/SkeletalMeshComponent.h"
#include "Containers/Ticker.h"
#include "Engine/SkeletalMesh.h"
#include "Editor.h"
#include "Engine/Selection.h"
#include "EditorViewportClient.h"
#include "EngineUtils.h"
#include "Framework/Docking/TabManager.h"
#include "HAL/IConsoleManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "PrimitiveDrawingUtils.h"
#include "Serialization/JsonSerializer.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "UnrealEdGlobals.h"
#include "Editor/UnrealEdEngine.h"

namespace
{
const FName DebugTab(TEXT("VamCharacterDebug"));
TWeakObjectPtr<AVamCharacterActor> DebugActor;
int32 SelectedBone=INDEX_NONE;
bool bPanelOpen=false;

AVamCharacterActor* CurrentActor()
{
    if (DebugActor.IsValid()) return DebugActor.Get();
    if (GEditor)
        if (auto* Actor=GEditor->GetSelectedActors()->GetTop<AVamCharacterActor>()) return Actor;
    return nullptr;
}

struct HVamBoneProxy : HComponentVisProxy
{
    DECLARE_HIT_PROXY();
    int32 BoneIndex;
    HVamBoneProxy(const UActorComponent* Component,int32 Index):HComponentVisProxy(Component,HPP_Wireframe),BoneIndex(Index){}
};
IMPLEMENT_HIT_PROXY(HVamBoneProxy,HComponentVisProxy);

class FVamBoneVisualizer final : public FComponentVisualizer
{
    TWeakObjectPtr<UVamCharacterComponent> Edited;
public:
    virtual void DrawVisualization(const UActorComponent* Component,const FSceneView*,FPrimitiveDrawInterface* PDI) override
    {
        if (!bPanelOpen) return;
        const auto* Character=Cast<UVamCharacterComponent>(Component);
        if (!Character || !Character->Body || !Character->Body->GetSkeletalMeshAsset()) return;
        if (auto* Actor=CurrentActor(); Actor && Actor->Character!=Character) return;
        const auto& Skeleton=Character->Body->GetSkeletalMeshAsset()->GetRefSkeleton();
        for (int32 Index=0;Index<Skeleton.GetRawBoneNum();++Index)
        {
            const FVector Position=Character->Body->GetBoneTransform(Index).GetTranslation();
            const int32 Parent=Skeleton.GetParentIndex(Index);
            if (Parent!=INDEX_NONE)
                PDI->DrawLine(Character->Body->GetBoneTransform(Parent).GetTranslation(),Position,FLinearColor(.1f,.7f,.85f),SDPG_Foreground,1.5f);
            PDI->SetHitProxy(new HVamBoneProxy(Component,Index));
            PDI->DrawPoint(Position,Index==SelectedBone?FLinearColor::Yellow:FLinearColor(.1f,.9f,.9f),Index==SelectedBone?14.f:9.f,SDPG_Foreground);
            PDI->SetHitProxy(nullptr);
        }
    }
    virtual bool VisProxyHandleClick(FEditorViewportClient*,HComponentVisProxy* Proxy,const FViewportClick&) override
    {
        if (!Proxy || !Proxy->IsA(HVamBoneProxy::StaticGetType())) return false;
        auto* Bone=static_cast<HVamBoneProxy*>(Proxy);
        Edited=Cast<UVamCharacterComponent>(const_cast<UActorComponent*>(Bone->Component.Get()));
        SelectedBone=Bone->BoneIndex;
        return Edited.IsValid();
    }
    virtual bool GetWidgetLocation(const FEditorViewportClient*,FVector& Location) const override
    {
        if (!Edited.IsValid() || !Edited->Body || SelectedBone==INDEX_NONE) return false;
        Location=Edited->Body->GetBoneTransform(SelectedBone).GetTranslation();
        return true;
    }
    virtual bool HandleInputDelta(FEditorViewportClient*,FViewport*,FVector& Translation,FRotator& Rotation,FVector&) override
    {
        if (!Edited.IsValid() || !Edited->Body || SelectedBone==INDEX_NONE) return false;
        const auto& Skeleton=Edited->Body->GetSkeletalMeshAsset()->GetRefSkeleton();
        if (!Skeleton.IsValidIndex(SelectedBone)) return false;
        const int32 Parent=Skeleton.GetParentIndex(SelectedBone);
        const FQuat ParentRotation=Parent==INDEX_NONE ? Edited->Body->GetComponentQuat() : Edited->Body->GetBoneTransform(Parent).GetRotation();
        FTransform Offset=Edited->GetDebugBoneOffset(SelectedBone);
        Offset.AddToTranslation(ParentRotation.Inverse().RotateVector(Translation));
        if (!Rotation.IsNearlyZero())
        {
            const FQuat LocalDelta=ParentRotation.Inverse()*Rotation.Quaternion()*ParentRotation;
            Offset.SetRotation((LocalDelta*Offset.GetRotation()).GetNormalized());
        }
        return Edited->SetDebugBoneOffset(SelectedBone,Offset);
    }
    virtual UActorComponent* GetEditedComponent() const override { return Edited.Get(); }
    virtual void EndEditing() override { Edited.Reset(); SelectedBone=INDEX_NONE; }
};

TSharedPtr<FVamBoneVisualizer> Visualizer;
FTSTicker::FDelegateHandle VisualizerRegistration;
bool bVisualizerRegistered=false;
struct FPanelRows { TSharedPtr<SVerticalBox> Box; FString Filter; };

float BoneAxis(int32 Axis)
{
    auto* Actor=CurrentActor();
    if (!Actor || SelectedBone==INDEX_NONE) return 0.f;
    const FVector Value=Actor->Character->GetDebugBoneOffset(SelectedBone).GetTranslation();
    return static_cast<float>(Value[Axis]);
}
void SetBoneAxis(int32 Axis,float Value)
{
    auto* Actor=CurrentActor();
    if (!Actor || SelectedBone==INDEX_NONE) return;
    FTransform Offset=Actor->Character->GetDebugBoneOffset(SelectedBone);
    FVector Translation=Offset.GetTranslation();Translation[Axis]=Value;
    Offset.SetTranslation(Translation);
    Actor->Character->SetDebugBoneOffset(SelectedBone,Offset);
}
float BoneRotationAxis(int32 Axis)
{
    auto* Actor=CurrentActor();
    if (!Actor || SelectedBone==INDEX_NONE) return 0.f;
    const FRotator Value=Actor->Character->GetDebugBoneOffset(SelectedBone).GetRotation().Rotator();
    return Axis==0?Value.Roll:Axis==1?Value.Pitch:Value.Yaw;
}
void SetBoneRotationAxis(int32 Axis,float Value)
{
    auto* Actor=CurrentActor();
    if (!Actor || SelectedBone==INDEX_NONE) return;
    FTransform Offset=Actor->Character->GetDebugBoneOffset(SelectedBone);
    FRotator Rotation=Offset.GetRotation().Rotator();
    if (Axis==0) Rotation.Roll=Value;else if (Axis==1) Rotation.Pitch=Value;else Rotation.Yaw=Value;
    Offset.SetRotation(Rotation.Quaternion());
    Actor->Character->SetDebugBoneOffset(SelectedBone,Offset);
}

void AddControls(TSharedRef<SVerticalBox> Rows,AVamCharacterActor* Actor,const FString& Filter=FString())
{
    auto* Character=Actor ? Actor->Character.Get() : nullptr;
    auto* Body=Character ? Character->Body.Get() : nullptr;
    auto* Definition=Character ? Character->Definition.Get() : nullptr;
    if (!Body || !Definition || !Body->GetSkeletalMeshAsset())
    {
        Rows->AddSlot().AutoHeight().Padding(8)[SNew(STextBlock).Text(FText::FromString(TEXT("选择并加载场景中的 VaM 人物后刷新。")))];
        return;
    }
    const auto& Skeleton=Body->GetSkeletalMeshAsset()->GetRefSkeleton();
    Rows->AddSlot().AutoHeight().Padding(8)[SNew(STextBlock).Text(FText::FromString(FString::Printf(TEXT("骨骼控制点 %d · 可在视口点击并拖动移动/旋转工具"),Skeleton.GetRawBoneNum())))];
    for (int32 Index=0;Index<Skeleton.GetRawBoneNum();++Index)
    {
        const FName Name=Skeleton.GetBoneName(Index);
        if (!Filter.IsEmpty() && !Name.ToString().Contains(Filter,ESearchCase::IgnoreCase)) continue;
        Rows->AddSlot().AutoHeight().Padding(3,1)[SNew(SButton)
            .Text(FText::FromName(Name))
            .OnClicked_Lambda([Index,Character](){SelectedBone=Index;return FReply::Handled();})];
    }
    Rows->AddSlot().AutoHeight().Padding(8)[SNew(STextBlock).Text(FText::FromString(FString::Printf(TEXT("Morph / 骨骼中心参数 %d · 拖动滑块预览"),Definition->Parameters.Num())))];
    for (const auto& Parameter:Definition->Parameters)
    {
        TWeakObjectPtr<UVamCharacterComponent> WeakCharacter=Character;
        const FName Name=Parameter.Target;
        const float Minimum=Parameter.Minimum,Maximum=Parameter.Maximum;
        const FString Label=Parameter.DisplayName.IsEmpty()?Name.ToString():Parameter.DisplayName;
        if (!Filter.IsEmpty() && !Label.Contains(Filter,ESearchCase::IgnoreCase) && !Name.ToString().Contains(Filter,ESearchCase::IgnoreCase)) continue;
        Rows->AddSlot().AutoHeight().Padding(4,2)[SNew(SHorizontalBox)
            +SHorizontalBox::Slot().FillWidth(.45f)[SNew(STextBlock).Text(FText::FromString(Label)).ToolTipText(FText::FromName(Name))]
            +SHorizontalBox::Slot().FillWidth(.4f)[SNew(SSlider)
                .Value_Lambda([WeakCharacter,Name,Minimum,Maximum](){const float Value=WeakCharacter.IsValid()?WeakCharacter->GetShapeState().Values.FindRef(Name):Minimum;return Maximum>Minimum?(Value-Minimum)/(Maximum-Minimum):0.f;})
                .OnValueChanged_Lambda([WeakCharacter,Name,Minimum,Maximum](float Alpha){if (WeakCharacter.IsValid()) WeakCharacter->SetParameter(Name,FMath::Lerp(Minimum,Maximum,Alpha));})]
            +SHorizontalBox::Slot().FillWidth(.15f)[SNew(STextBlock).Text_Lambda([WeakCharacter,Name](){return FText::FromString(WeakCharacter.IsValid()?FString::Printf(TEXT("%.3f"),WeakCharacter->GetShapeState().Values.FindRef(Name)):TEXT("-"));})]];
    }
}

TSharedRef<SDockTab> SpawnPanel(const FSpawnTabArgs&)
{
    bPanelOpen=true;
    UE_LOG(LogTemp,Display,TEXT("VAM_DEBUG_PANEL_OPENED"));
    TSharedRef<FPanelRows> State=MakeShared<FPanelRows>();
    TSharedRef<SDockTab> Tab=SNew(SDockTab).TabRole(ETabRole::NomadTab)
    [SNew(SVerticalBox)
        +SVerticalBox::Slot().AutoHeight().Padding(8)[SNew(STextBlock).Text(FText::FromString(TEXT("人物调试 · 场景控制点")))]
        +SVerticalBox::Slot().AutoHeight().Padding(8)[SNew(STextBlock).Text_Lambda([](){
            auto* Actor=CurrentActor();return FText::FromString(Actor?TEXT("当前人物：")+Actor->GetActorLabel():TEXT("当前人物：未选择"));})]
        +SVerticalBox::Slot().AutoHeight().Padding(8)[SNew(SHorizontalBox)
            +SHorizontalBox::Slot().AutoWidth()[SNew(SButton).Text(FText::FromString(TEXT("使用选中人物 / 刷新"))).OnClicked_Lambda([State](){
                DebugActor=GEditor?GEditor->GetSelectedActors()->GetTop<AVamCharacterActor>():nullptr;
                SelectedBone=INDEX_NONE;if (State->Box.IsValid()){State->Box->ClearChildren();AddControls(State->Box.ToSharedRef(),DebugActor.Get(),State->Filter);}return FReply::Handled();})]
            +SHorizontalBox::Slot().AutoWidth().Padding(8,0)[SNew(SButton).Text(FText::FromString(TEXT("加载最新导入人物"))).OnClicked_Lambda([State](){
                const FString Plugin=IPluginManager::Get().FindPlugin(TEXT("VamResourceBrowser"))->GetBaseDir();
                FString Raw;TSharedPtr<FJsonObject> Report;
                if (!FFileHelper::LoadFileToString(Raw,*(Plugin/TEXT("Saved/NativeBuild/latest-native-assets.json"))) ||
                    !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Raw),Report) || !Report.IsValid()) return FReply::Handled();
                FString Path=Report->GetStringField(TEXT("definition"));Path+=TEXT(".")+FPackageName::GetLongPackageAssetName(Path);
                auto* Definition=LoadObject<UVamCharacterDefinition>(nullptr,*Path);
                UWorld* World=GEditor?GEditor->GetEditorWorldContext().World():nullptr;
                if (!World || !Definition) return FReply::Handled();
                FActorSpawnParameters Spawn;Spawn.ObjectFlags=RF_Transient;
                auto* Actor=World->SpawnActor<AVamCharacterActor>(Spawn);
                if (!Actor) return FReply::Handled();
                Actor->Character->Definition=Definition;Actor->LoadCharacter();
                GEditor->SelectNone(false,true,false);GEditor->SelectActor(Actor,true,true);
                DebugActor=Actor;SelectedBone=INDEX_NONE;
                if (State->Box.IsValid()){State->Box->ClearChildren();AddControls(State->Box.ToSharedRef(),Actor,State->Filter);}
                const TWeakObjectPtr<AVamCharacterActor> PendingActor=Actor;
                const double Deadline=FPlatformTime::Seconds()+20.0;
                FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([State,PendingActor,Deadline](float){
                    if (!PendingActor.IsValid() || FPlatformTime::Seconds()>Deadline) return false;
                    if (!PendingActor->Character->Body) return true;
                    if (State->Box.IsValid()){State->Box->ClearChildren();AddControls(State->Box.ToSharedRef(),PendingActor.Get(),State->Filter);}
                    return false;
                }),.5f);
                return FReply::Handled();})]
            +SHorizontalBox::Slot().AutoWidth().Padding(8,0)[SNew(SButton).Text(FText::FromString(TEXT("重置骨骼姿态"))).OnClicked_Lambda([](){if (auto* Actor=CurrentActor()) Actor->Character->ResetDebugBoneOffsets();return FReply::Handled();})]
            +SHorizontalBox::Slot().AutoWidth().Padding(8,0)[SNew(SButton).Text(FText::FromString(TEXT("恢复导入形状"))).OnClicked_Lambda([](){if (auto* Actor=CurrentActor()) Actor->Character->ResetToImportedAppearance();return FReply::Handled();})]]
        +SVerticalBox::Slot().AutoHeight().Padding(8)[SNew(STextBlock).Text_Lambda([](){
            auto* Actor=CurrentActor();auto* Body=Actor?Actor->Character->Body.Get():nullptr;
            if (!Body || SelectedBone==INDEX_NONE || !Body->GetSkeletalMeshAsset()) return FText::FromString(TEXT("视口中的青色点为骨骼；点击点后拖动移动/旋转工具。"));
            const auto& Skeleton=Body->GetSkeletalMeshAsset()->GetRefSkeleton();
            return FText::FromString(Skeleton.IsValidIndex(SelectedBone)?TEXT("选中骨骼：")+Skeleton.GetBoneName(SelectedBone).ToString():TEXT("未选中骨骼"));})]
        +SVerticalBox::Slot().AutoHeight().Padding(8)[SNew(SHorizontalBox)
            +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(STextBlock).Text(FText::FromString(TEXT("局部位移 cm")))]
            +SHorizontalBox::Slot().AutoWidth().Padding(6,0)[SNew(STextBlock).Text(FText::FromString(TEXT("X")))]
            +SHorizontalBox::Slot().FillWidth(1)[SNew(SSpinBox<float>).MinValue(-500.f).MaxValue(500.f).Value_Lambda([](){return BoneAxis(0);}).OnValueChanged_Lambda([](float Value){SetBoneAxis(0,Value);})]
            +SHorizontalBox::Slot().AutoWidth().Padding(6,0)[SNew(STextBlock).Text(FText::FromString(TEXT("Y")))]
            +SHorizontalBox::Slot().FillWidth(1)[SNew(SSpinBox<float>).MinValue(-500.f).MaxValue(500.f).Value_Lambda([](){return BoneAxis(1);}).OnValueChanged_Lambda([](float Value){SetBoneAxis(1,Value);})]
            +SHorizontalBox::Slot().AutoWidth().Padding(6,0)[SNew(STextBlock).Text(FText::FromString(TEXT("Z")))]
            +SHorizontalBox::Slot().FillWidth(1)[SNew(SSpinBox<float>).MinValue(-500.f).MaxValue(500.f).Value_Lambda([](){return BoneAxis(2);}).OnValueChanged_Lambda([](float Value){SetBoneAxis(2,Value);})]
            +SHorizontalBox::Slot().AutoWidth().Padding(6,0)[SNew(SButton).Text(FText::FromString(TEXT("重置此骨骼"))).OnClicked_Lambda([](){if (auto* Actor=CurrentActor()) Actor->Character->SetDebugBoneOffset(SelectedBone,FTransform::Identity);return FReply::Handled();})]]
        +SVerticalBox::Slot().AutoHeight().Padding(8)[SNew(SHorizontalBox)
            +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(STextBlock).Text(FText::FromString(TEXT("局部旋转 °")))]
            +SHorizontalBox::Slot().AutoWidth().Padding(6,0)[SNew(STextBlock).Text(FText::FromString(TEXT("X")))]
            +SHorizontalBox::Slot().FillWidth(1)[SNew(SSpinBox<float>).MinValue(-360.f).MaxValue(360.f).Value_Lambda([](){return BoneRotationAxis(0);}).OnValueChanged_Lambda([](float Value){SetBoneRotationAxis(0,Value);})]
            +SHorizontalBox::Slot().AutoWidth().Padding(6,0)[SNew(STextBlock).Text(FText::FromString(TEXT("Y")))]
            +SHorizontalBox::Slot().FillWidth(1)[SNew(SSpinBox<float>).MinValue(-360.f).MaxValue(360.f).Value_Lambda([](){return BoneRotationAxis(1);}).OnValueChanged_Lambda([](float Value){SetBoneRotationAxis(1,Value);})]
            +SHorizontalBox::Slot().AutoWidth().Padding(6,0)[SNew(STextBlock).Text(FText::FromString(TEXT("Z")))]
            +SHorizontalBox::Slot().FillWidth(1)[SNew(SSpinBox<float>).MinValue(-360.f).MaxValue(360.f).Value_Lambda([](){return BoneRotationAxis(2);}).OnValueChanged_Lambda([](float Value){SetBoneRotationAxis(2,Value);})]]
        +SVerticalBox::Slot().AutoHeight().Padding(8)[SNew(STextBlock).Text(FText::FromString(TEXT("Stage06 · 运行时与惯性见证")))]
        +SVerticalBox::Slot().AutoHeight().Padding(8)[SNew(SHorizontalBox)
            +SHorizontalBox::Slot().AutoWidth()[SNew(SButton).Text(FText::FromString(TEXT("暂停/继续见证时钟"))).OnClicked_Lambda([](){if(auto* A=CurrentActor()) if(A->Motion) A->Motion->SetPreviewPaused(!A->Motion->GetClock().bPaused);return FReply::Handled();})]
            +SHorizontalBox::Slot().AutoWidth().Padding(4,0)[SNew(SButton).Text(FText::FromString(TEXT("单步"))).OnClicked_Lambda([](){if(auto* A=CurrentActor()) if(A->Motion) A->Motion->StepPreview();return FReply::Handled();})]
            +SHorizontalBox::Slot().AutoWidth().Padding(4,0)[SNew(SButton).Text(FText::FromString(TEXT("重置见证"))).OnClicked_Lambda([](){if(auto* A=CurrentActor()) if(A->Motion) A->Motion->ResetPreview();return FReply::Handled();})]]
        +SVerticalBox::Slot().AutoHeight().Padding(8)[SNew(SHorizontalBox)
            +SHorizontalBox::Slot().AutoWidth()[SNew(SButton).Text(FText::FromString(TEXT("受控姿态"))).OnClicked_Lambda([](){if(auto* A=CurrentActor()) if(A->Interaction) A->Interaction->SetPhysicalMode(EVamPhysicalMode::Controlled);return FReply::Handled();})]
            +SHorizontalBox::Slot().AutoWidth().Padding(4,0)[SNew(SButton).Text(FText::FromString(TEXT("布娃娃"))).OnClicked_Lambda([](){if(auto* A=CurrentActor()) if(A->Interaction) A->Interaction->SetPhysicalMode(EVamPhysicalMode::Ragdoll);return FReply::Handled();})]]
        +SVerticalBox::Slot().AutoHeight().Padding(8)[SNew(STextBlock).Text_Lambda([](){
            auto* A=CurrentActor();
            if(!A || !A->Motion) return FText::FromString(TEXT("未选择运行时人物"));
            const auto Clock=A->Motion->GetClock();const auto Sample=A->Motion->GetMotion();
            const TCHAR* Mode=TEXT("无物理组件");
            if(A->Interaction) Mode=A->Interaction->Mode==EVamPhysicalMode::Ragdoll?TEXT("布娃娃"):
                A->Interaction->Mode==EVamPhysicalMode::LocalResponse?TEXT("局部响应"):TEXT("受控");
            return FText::FromString(FString::Printf(TEXT("%s · 速度 %.1f cm/s · 加速度 %.1f cm/s² · 子步 %d · 丢弃 %d · 见证时钟%s"),
                Mode,Sample.LinearVelocity.Size(),Sample.LinearAcceleration.Size(),Clock.LastSteps,Clock.DroppedSteps,Clock.bPaused?TEXT("暂停"):TEXT("运行")));})]
        +SVerticalBox::Slot().AutoHeight().Padding(8)[SNew(STextBlock).Text(FText::FromString(TEXT("青色点为全部骨骼控制点；PIE 的 Stage06 预览场景可左键抓取或摆姿、右键连续拖动。黄色点为低成本惯性见证，不代表全身软体。编辑器普通 Gizmo 移动不会自动产生模拟速度。"))).AutoWrapText(true)]
        +SVerticalBox::Slot().AutoHeight().Padding(8)[SNew(SSearchBox).HintText(FText::FromString(TEXT("筛选骨骼或 Morph 名称"))).OnTextChanged_Lambda([State](const FText& Text){
            State->Filter=Text.ToString();if (State->Box.IsValid()){State->Box->ClearChildren();AddControls(State->Box.ToSharedRef(),CurrentActor(),State->Filter);}})]
        +SVerticalBox::Slot().FillHeight(1)[SNew(SScrollBox)+SScrollBox::Slot()[SAssignNew(State->Box,SVerticalBox)]]];
    AddControls(State->Box.ToSharedRef(),CurrentActor());
    Tab->SetOnTabClosed(SDockTab::FOnTabClosedCallback::CreateLambda([](TSharedRef<SDockTab>){bPanelOpen=false;DebugActor.Reset();SelectedBone=INDEX_NONE;}));
    return Tab;
}
}

void RegisterVamDebugPanel()
{
    FGlobalTabmanager::Get()->RegisterNomadTabSpawner(DebugTab,FOnSpawnTab::CreateStatic(&SpawnPanel))
        .SetDisplayName(FText::FromString(TEXT("VaM 人物调试"))).SetMenuType(ETabSpawnerMenuType::Hidden);
    if (IsRunningCommandlet()) return;
    Visualizer=MakeShared<FVamBoneVisualizer>();
    auto Register=[](float)
    {
        if (!GUnrealEd) return true;
        GUnrealEd->RegisterComponentVisualizer(UVamCharacterComponent::StaticClass()->GetFName(),Visualizer);
        Visualizer->OnRegister();bVisualizerRegistered=true;
        VisualizerRegistration.Reset();return false;
    };
    if (GUnrealEd) Register(0.f);
    else VisualizerRegistration=FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(Register),.5f);
}
void UnregisterVamDebugPanel()
{
    if (auto Tab=FGlobalTabmanager::Get()->FindExistingLiveTab(FTabId(DebugTab))) Tab->RequestCloseTab();
    if (VisualizerRegistration.IsValid()) FTSTicker::GetCoreTicker().RemoveTicker(VisualizerRegistration);
    VisualizerRegistration.Reset();
    if (bVisualizerRegistered && GUnrealEd) GUnrealEd->UnregisterComponentVisualizer(UVamCharacterComponent::StaticClass()->GetFName());
    bVisualizerRegistered=false;
    Visualizer.Reset();FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(DebugTab);
}
void OpenVamDebugPanel() { FGlobalTabmanager::Get()->TryInvokeTab(FTabId(DebugTab)); }
static FAutoConsoleCommand OpenDebugPanelCommand(TEXT("Vam.OpenDebugPanel"),
    TEXT("Open the VaM character debug panel"),FConsoleCommandDelegate::CreateStatic(&OpenVamDebugPanel));

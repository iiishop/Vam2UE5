#include "VamDebugPanel.h"
#include "VamCharacterActor.h"
#include "VamSoftTissueComponent.h"
#include "VamCharacterComponent.h"
#include "VamCharacterDefinition.h"
#include "VamRigProfile.h"
#include "VamMotionComponent.h"
#include "VamInteractionComponent.h"
#include "VamActivePoseComponent.h"
#include "VamRuntimeConfiguration.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "Engine/Blueprint.h"
#include "ComponentVisualizer.h"
#include "Components/SkeletalMeshComponent.h"
#include "Containers/Ticker.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Editor.h"
#include "Engine/Selection.h"
#include "EditorViewportClient.h"
#include "SEditorViewport.h"
#include "Slate/SceneViewport.h"
#include "EngineUtils.h"
#include "Framework/Docking/TabManager.h"
#include "HAL/IConsoleManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "PrimitiveDrawingUtils.h"
#include "InputCoreTypes.h"
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

int32 RootBoneIndex(const UVamCharacterComponent* Character)
{
    if (!Character || !Character->Body || !Character->Body->GetSkeletalMeshAsset()) return INDEX_NONE;
    if (const UVamRigProfile* Profile=Character->RigProfile.Get())
    {
        const int32 Mapped=Character->Body->GetBoneIndex(Profile->BoneForSemantic(TEXT("root")));
        if (Mapped!=INDEX_NONE) return Mapped;
    }
    return Character->Body->GetNumBones()>0 ? 0 : INDEX_NONE;
}

bool IsPosePoint(const UVamCharacterComponent* Character,int32 BoneIndex)
{
    return Character && BoneIndex!=RootBoneIndex(Character) && Character->IsPoseControlBone(BoneIndex);
}

FVector ControlPointWorld(const UVamCharacterComponent* Character,int32 BoneIndex)
{
    FVector Position=Character->Body->GetBoneTransform(BoneIndex).GetTranslation();
    if (BoneIndex==RootBoneIndex(Character) && Character->GetOwner())
        Position+=Character->GetOwner()->GetActorRightVector()*8.f;
    return Position;
}

void MoveRootTo(AVamCharacterActor* Actor,const FVector& Location)
{
    if (!Actor) return;
    if (Actor->GetWorld() && Actor->GetWorld()->IsGameWorld() && Actor->Motion)
    {
        if (Actor->Interaction && Actor->Interaction->Mode==EVamPhysicalMode::Controlled)
        {
            if (const UVamRigProfile* Profile=Actor->Character ? Actor->Character->RigProfile.Get() : nullptr)
            {
                const FName Pelvis=Profile->BoneForSemantic(TEXT("pelvis"));
                const FName Spine=Profile->BoneForSemantic(TEXT("spine"));
                if (!Pelvis.IsNone() && !Spine.IsNone()) Actor->Interaction->SetRootMotionResponse(Pelvis,Spine);
            }
        }
        FTransform Next=Actor->GetActorTransform();
        Next.SetLocation(Location);
        Actor->Motion->MoveContinuously(Next,Actor->GetWorld()->GetTimeSeconds());
    }
    else Actor->SetActorLocation(Location,false,nullptr,ETeleportType::None);
}

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
    TWeakObjectPtr<UVamCharacterComponent> HoveredComponent;
    TWeakObjectPtr<UVamCharacterComponent> PressedComponent;
    int32 HoveredBone=INDEX_NONE;
    int32 PressedBone=INDEX_NONE;
    bool bDragging=false;
    FTSTicker::FDelegateHandle HoverTicker;

    void RedrawControls() const
    {
        if (GEditor) GEditor->RedrawLevelEditingViewports(false);
    }

    bool RefreshHover(float)
    {
        const int32 PreviousPressedBone=PressedBone;
        UVamCharacterComponent* PreviousPressedComponent=PressedComponent.Get();
        int32 NextBone=INDEX_NONE;
        UVamCharacterComponent* NextComponent=nullptr;
        bool bLeftHeld=false;
        if (bPanelOpen && GEditor)
        {
            FViewport* Viewport=GEditor->GetActiveViewport();
            bool bInsideEditorViewport=false;
            for (FEditorViewportClient* Client:GEditor->GetAllViewportClients())
            {
                if (Client)
                {
                    const TSharedPtr<SEditorViewport> Widget=Client->GetEditorViewportWidget();
                    if (Widget.IsValid() && Widget->GetSceneViewport().Get()==Viewport)
                    {
                        bInsideEditorViewport=Widget->IsHovered();
                        break;
                    }
                }
            }
            if (Viewport && bInsideEditorViewport)
            {
                bLeftHeld=Viewport->KeyState(EKeys::LeftMouseButton);
                if (!bDragging && PressedBone==INDEX_NONE)
                {
                    const int32 X=Viewport->GetMouseX(),Y=Viewport->GetMouseY();
                    const FIntPoint Size=Viewport->GetSizeXY();
                    if (X>=0 && Y>=0 && X<Size.X && Y<Size.Y)
                    {
                        HHitProxy* Hit=Viewport->GetHitProxy(X,Y);
                        if (Hit && Hit->IsA(HVamBoneProxy::StaticGetType()))
                        {
                            auto* Bone=static_cast<HVamBoneProxy*>(Hit);
                            NextComponent=Cast<UVamCharacterComponent>(const_cast<UActorComponent*>(Bone->Component.Get()));
                            if (const AVamCharacterActor* Actor=CurrentActor(); !Actor || !NextComponent || Actor->Character.Get()!=NextComponent)
                                NextComponent=nullptr;
                            else NextBone=Bone->BoneIndex;
                        }
                    }
                }
            }
        }
        if (bLeftHeld && PressedBone==INDEX_NONE && NextBone!=INDEX_NONE)
        {
            PressedBone=NextBone;
            PressedComponent=NextComponent;
        }
        else if (!bLeftHeld)
        {
            PressedBone=INDEX_NONE;
            PressedComponent.Reset();
        }
        if (bLeftHeld && PressedBone!=INDEX_NONE)
        {
            NextBone=PressedBone;
            NextComponent=PressedComponent.Get();
        }
        const bool bHoverChanged=HoveredBone!=NextBone || HoveredComponent.Get()!=NextComponent;
        const bool bPressChanged=PressedBone!=PreviousPressedBone || PressedComponent.Get()!=PreviousPressedComponent;
        if (bHoverChanged || bPressChanged)
        {
            HoveredBone=NextBone;
            HoveredComponent=NextComponent;
            RedrawControls();
        }
        return true;
    }
public:
    virtual ~FVamBoneVisualizer() override
    {
        if (HoverTicker.IsValid()) FTSTicker::GetCoreTicker().RemoveTicker(HoverTicker);
    }
    virtual void OnRegister() override
    {
        HoverTicker=FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this,&FVamBoneVisualizer::RefreshHover),.033f);
    }
    virtual void DrawVisualization(const UActorComponent* Component,const FSceneView*,FPrimitiveDrawInterface* PDI) override
    {
        if (!bPanelOpen) return;
        const auto* Character=Cast<UVamCharacterComponent>(Component);
        if (!Character || !Character->Body || !Character->Body->GetSkeletalMeshAsset()) return;
        if (auto* Actor=CurrentActor(); Actor && Actor->Character!=Character) return;
        const auto& Skeleton=Character->Body->GetSkeletalMeshAsset()->GetRefSkeleton();
        const int32 Root=RootBoneIndex(Character);
        for (int32 Index=0;Index<Skeleton.GetRawBoneNum();++Index)
        {
            if (Index!=Root && !IsPosePoint(Character,Index)) continue;
            const FVector Position=ControlPointWorld(Character,Index);
            const int32 Parent=Skeleton.GetParentIndex(Index);
            if (Parent!=INDEX_NONE && (Parent==Root || IsPosePoint(Character,Parent)))
                PDI->DrawLine(ControlPointWorld(Character,Parent),Position,FLinearColor(.1f,.7f,.85f),SDPG_Foreground,1.5f);
            PDI->SetHitProxy(new HVamBoneProxy(Component,Index));
            const bool bPressed=(bDragging && Edited.Get()==Character && Index==SelectedBone)
                || (PressedComponent.Get()==Character && Index==PressedBone);
            const bool bHovered=HoveredComponent.Get()==Character && Index==HoveredBone;
            const FLinearColor Base=Index==Root ? FLinearColor(1.f,.45f,.05f) : FLinearColor(.1f,.9f,.9f);
            if (bPressed)
            {
                PDI->DrawPoint(Position,Index==Root ? FLinearColor(1.f,.12f,.08f) : FLinearColor(1.f,.08f,.7f),36.f,SDPG_Foreground);
                PDI->DrawPoint(Position,FLinearColor(.06f,.04f,.08f),25.f,SDPG_Foreground);
                PDI->DrawPoint(Position,FLinearColor::Yellow,18.f,SDPG_Foreground);
            }
            else if (bHovered)
            {
                PDI->DrawPoint(Position,Base,29.f,SDPG_Foreground);
                PDI->DrawPoint(Position,FLinearColor(.04f,.08f,.1f),20.f,SDPG_Foreground);
                PDI->DrawPoint(Position,FLinearColor::White,14.f,SDPG_Foreground);
            }
            else if (Index==SelectedBone)
            {
                PDI->DrawPoint(Position,FLinearColor::Yellow,19.f,SDPG_Foreground);
                PDI->DrawPoint(Position,Base,11.f,SDPG_Foreground);
            }
            else PDI->DrawPoint(Position,Base,Index==Root?14.f:9.f,SDPG_Foreground);
            PDI->SetHitProxy(nullptr);
        }
    }
    virtual void DrawVisualizationHUD(const UActorComponent* Component,const FViewport*,const FSceneView* View,FCanvas* Canvas) override
    {
        if (!bPanelOpen || !View || !Canvas || !GEngine) return;
        const auto* Character=Cast<UVamCharacterComponent>(Component);
        if (!Character || !Character->Body || !Character->Body->GetSkeletalMeshAsset()) return;
        const bool bDraggingThis=bDragging && Edited.Get()==Character && SelectedBone!=INDEX_NONE;
        const bool bPressed=bDraggingThis || (PressedComponent.Get()==Character && PressedBone!=INDEX_NONE);
        const int32 FocusBone=bDraggingThis ? SelectedBone : bPressed ? PressedBone : HoveredComponent.Get()==Character ? HoveredBone : INDEX_NONE;
        const auto& Skeleton=Character->Body->GetSkeletalMeshAsset()->GetRefSkeleton();
        if (!Skeleton.IsValidIndex(FocusBone)) return;
        FVector2D Pixel;
        if (!View->WorldToPixel(ControlPointWorld(Character,FocusBone),Pixel)) return;
        const bool bRoot=FocusBone==RootBoneIndex(Character);
        const FString Label=FString::Printf(TEXT("%s  ·  %s"),*Skeleton.GetBoneName(FocusBone).ToString(),
            bPressed ? bRoot ? TEXT("正在移动人物") : TEXT("正在旋转关节") : bRoot ? TEXT("拖动移动人物") : TEXT("拖动旋转关节"));
        Canvas->DrawShadowedString(Pixel.X+20.f,Pixel.Y-27.f,*Label,GEngine->GetSmallFont(),bPressed ? FLinearColor::Yellow : FLinearColor::White);
    }
    virtual bool VisProxyHandleClick(FEditorViewportClient* ViewportClient,HComponentVisProxy* Proxy,const FViewportClick&) override
    {
        if (!Proxy || !Proxy->IsA(HVamBoneProxy::StaticGetType())) return false;
        auto* Bone=static_cast<HVamBoneProxy*>(Proxy);
        Edited=Cast<UVamCharacterComponent>(const_cast<UActorComponent*>(Bone->Component.Get()));
        if (!Edited.IsValid() || (Bone->BoneIndex!=RootBoneIndex(Edited.Get()) && !IsPosePoint(Edited.Get(),Bone->BoneIndex))) return false;
        SelectedBone=Bone->BoneIndex;
        if (ViewportClient) ViewportClient->SetWidgetMode(SelectedBone==RootBoneIndex(Edited.Get()) ? UE::Widget::WM_Translate : UE::Widget::WM_Rotate);
        RedrawControls();
        return true;
    }
    virtual bool HandleInputKey(FEditorViewportClient*,FViewport*,FKey Key,EInputEvent Event) override
    {
        if (Key==EKeys::LeftMouseButton && Event==IE_Pressed && HoveredBone!=INDEX_NONE && HoveredComponent.IsValid())
        {
            PressedBone=HoveredBone;
            PressedComponent=HoveredComponent;
            RedrawControls();
        }
        else if (Key==EKeys::LeftMouseButton && Event==IE_Released)
        {
            bDragging=false;
            PressedBone=INDEX_NONE;
            PressedComponent.Reset();
            RedrawControls();
        }
        return false;
    }
    virtual void TrackingStarted(FEditorViewportClient* ViewportClient) override
    {
        const TSharedPtr<SEditorViewport> Widget=ViewportClient ? ViewportClient->GetEditorViewportWidget() : nullptr;
        FViewport* Viewport=Widget.IsValid() ? Widget->GetSceneViewport().Get() : nullptr;
        if (Edited.IsValid() && SelectedBone!=INDEX_NONE && Viewport && Viewport->KeyState(EKeys::LeftMouseButton))
        {
            bDragging=true;
            RedrawControls();
        }
    }
    virtual void TrackingStopped(FEditorViewportClient*,bool) override
    {
        if (bDragging)
        {
            bDragging=false;
            RedrawControls();
        }
    }
    virtual bool GetWidgetLocation(const FEditorViewportClient*,FVector& Location) const override
    {
        if (!Edited.IsValid() || !Edited->Body || SelectedBone==INDEX_NONE) return false;
        Location=ControlPointWorld(Edited.Get(),SelectedBone);
        return true;
    }
    virtual bool HandleInputDelta(FEditorViewportClient*,FViewport*,FVector& Translation,FRotator& Rotation,FVector&) override
    {
        if (!Edited.IsValid() || !Edited->Body || SelectedBone==INDEX_NONE) return false;
        const auto& Skeleton=Edited->Body->GetSkeletalMeshAsset()->GetRefSkeleton();
        if (!Skeleton.IsValidIndex(SelectedBone)) return false;
        if (SelectedBone==RootBoneIndex(Edited.Get()))
        {
            if (!Translation.IsNearlyZero()) MoveRootTo(Cast<AVamCharacterActor>(Edited->GetOwner()),Edited->GetOwner()->GetActorLocation()+Translation);
            return true;
        }
        if (!IsPosePoint(Edited.Get(),SelectedBone)) return false;
        if (Rotation.IsNearlyZero()) return true;
        const int32 Parent=Skeleton.GetParentIndex(SelectedBone);
        const FQuat ParentRotation=Parent==INDEX_NONE ? Edited->Body->GetComponentQuat() : Edited->Body->GetBoneTransform(Parent).GetRotation();
        const FQuat LocalDelta=ParentRotation.Inverse()*Rotation.Quaternion()*ParentRotation;
        const FQuat Current=Edited->GetPoseControlRotation(SelectedBone).Quaternion();
        return Edited->SetPoseControlRotation(SelectedBone,(LocalDelta*Current).GetNormalized().Rotator());
    }
    virtual UActorComponent* GetEditedComponent() const override { return Edited.Get(); }
    virtual void EndEditing() override { Edited.Reset(); SelectedBone=INDEX_NONE; PressedComponent.Reset(); PressedBone=INDEX_NONE; bDragging=false; RedrawControls(); }
};

TSharedPtr<FVamBoneVisualizer> Visualizer;
FTSTicker::FDelegateHandle VisualizerRegistration;
bool bVisualizerRegistered=false;
struct FPanelRows { TSharedPtr<SVerticalBox> Box; FString Filter; };

bool RootSelected()
{
    auto* Actor=CurrentActor();
    return Actor && Actor->Character && SelectedBone==RootBoneIndex(Actor->Character.Get());
}
bool PoseSelected()
{
    auto* Actor=CurrentActor();
    return Actor && Actor->Character && IsPosePoint(Actor->Character.Get(),SelectedBone);
}
float RootAxis(int32 Axis)
{
    auto* Actor=CurrentActor();
    return RootSelected() ? static_cast<float>(Actor->GetActorLocation()[Axis]) : 0.f;
}
void SetRootAxis(int32 Axis,float Value)
{
    auto* Actor=CurrentActor();
    if (!RootSelected()) return;
    FVector Position=Actor->GetActorLocation();
    Position[Axis]=Value;
    MoveRootTo(Actor,Position);
}
float BoneRotationAxis(int32 Axis)
{
    auto* Actor=CurrentActor();
    if (!PoseSelected()) return 0.f;
    const FRotator Value=Actor->Character->GetPoseControlRotation(SelectedBone);
    return Axis==0?Value.Roll:Axis==1?Value.Pitch:Value.Yaw;
}
void SetBoneRotationAxis(int32 Axis,float Value)
{
    auto* Actor=CurrentActor();
    if (!PoseSelected()) return;
    FRotator Rotation=Actor->Character->GetPoseControlRotation(SelectedBone);
    if (Axis==0) Rotation.Roll=Value;else if (Axis==1) Rotation.Pitch=Value;else Rotation.Yaw=Value;
    Actor->Character->SetPoseControlRotation(SelectedBone,Rotation);
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
    const int32 Root=RootBoneIndex(Character);
    int32 PoseCount=0;
    for (int32 Index=0;Index<Skeleton.GetRawBoneNum();++Index) if (IsPosePoint(Character,Index)) ++PoseCount;
    Rows->AddSlot().AutoHeight().Padding(8)[SNew(STextBlock).Text(FText::FromString(FString::Printf(TEXT("根点平移 1 · 骨骼旋转 %d · 点击人物上的控制点使用对应工具"),PoseCount)))];
    if (Root!=INDEX_NONE && (Filter.IsEmpty() || FString(TEXT("root")).Contains(Filter,ESearchCase::IgnoreCase) || Skeleton.GetBoneName(Root).ToString().Contains(Filter,ESearchCase::IgnoreCase)))
        Rows->AddSlot().AutoHeight().Padding(3,1)[SNew(SButton)
            .Text(FText::FromString(TEXT("root · 移动整个人物")))
            .ToolTipText(FText::FromName(Skeleton.GetBoneName(Root)))
            .OnClicked_Lambda([Root](){SelectedBone=Root;return FReply::Handled();})];
    for (int32 Index=0;Index<Skeleton.GetRawBoneNum();++Index)
    {
        if (!IsPosePoint(Character,Index)) continue;
        const FName Name=Skeleton.GetBoneName(Index);
        if (!Filter.IsEmpty() && !Name.ToString().Contains(Filter,ESearchCase::IgnoreCase)) continue;
        Rows->AddSlot().AutoHeight().Padding(3,1)[SNew(SButton)
            .Text(FText::FromName(Name))
            .OnClicked_Lambda([Index,Character](){SelectedBone=Index;return FReply::Handled();})];
    }
    Rows->AddSlot().AutoHeight().Padding(8)[SNew(STextBlock).Text(FText::FromString(FString::Printf(TEXT("独立形状 / Morph 调试 %d · 不属于摆姿控制点"),Definition->Parameters.Num())))];
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
            +SHorizontalBox::Slot().AutoWidth().Padding(8,0)[SNew(SButton).Text(FText::FromString(TEXT("旧样例：加载最新导入人物"))).OnClicked_Lambda([State](){
                const FString Plugin=IPluginManager::Get().FindPlugin(TEXT("VamResourceBrowser"))->GetBaseDir();
                FString Raw;TSharedPtr<FJsonObject> Report;
                if (!FFileHelper::LoadFileToString(Raw,*(Plugin/TEXT("Saved/NativeBuild/latest-native-assets.json"))) ||
                    !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Raw),Report) || !Report.IsValid()) return FReply::Handled();
                FString Path=Report->GetStringField(TEXT("definition"));Path+=TEXT(".")+FPackageName::GetLongPackageAssetName(Path);
                auto* Definition=LoadObject<UVamCharacterDefinition>(nullptr,*Path);
                UWorld* World=GEditor?GEditor->GetEditorWorldContext().World():nullptr;
                if (!World || !Definition) return FReply::Handled();
                UClass* CharacterClass=LoadClass<AVamCharacterActor>(nullptr,TEXT("/Game/VamStage06/BP_VamCharacter.BP_VamCharacter_C"));
                if (!CharacterClass) { UE_LOG(LogTemp,Warning,TEXT("VAM_DEBUG_PANEL_MISSING_STAGE06_BLUEPRINT")); return FReply::Handled(); }
                FActorSpawnParameters Spawn;Spawn.ObjectFlags=RF_Transient;
                auto* Actor=World->SpawnActor<AVamCharacterActor>(CharacterClass,FVector::ZeroVector,FRotator::ZeroRotator,Spawn);
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
            +SHorizontalBox::Slot().AutoWidth().Padding(8,0)[SNew(SButton).Text(FText::FromString(TEXT("重置骨骼姿态"))).OnClicked_Lambda([](){if (auto* Actor=CurrentActor()) {Actor->Character->ResetPoseControlRotations();Actor->Character->ResetDebugBoneOffsets();}return FReply::Handled();})]
            +SHorizontalBox::Slot().AutoWidth().Padding(8,0)[SNew(SButton).Text(FText::FromString(TEXT("恢复导入形状"))).OnClicked_Lambda([](){if (auto* Actor=CurrentActor()) Actor->Character->ResetToImportedAppearance();return FReply::Handled();})]]
        +SVerticalBox::Slot().AutoHeight().Padding(8)[SNew(SButton).Text(FText::FromString(TEXT("加载内容浏览器所选 RuntimeConfiguration"))).OnClicked_Lambda([State](){
            TArray<FAssetData> Assets;FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser")).Get().GetSelectedAssets(Assets);
            if(Assets.Num()!=1) return FReply::Handled();
            auto* Config=Cast<UVamRuntimeConfiguration>(Assets[0].GetAsset());
            if(!Config || !Config->bIndependentReloadVerified) return FReply::Handled();
            TSharedPtr<FJsonObject> Receipt;FString BlueprintPath;
            if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Config->ReceiptJson),Receipt) || !Receipt.IsValid() ||
                !Receipt->TryGetStringField(TEXT("blueprint"),BlueprintPath)) return FReply::Handled();
            auto* Blueprint=LoadObject<UBlueprint>(nullptr,*BlueprintPath);
            UWorld* World=GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
            if(!World || !Blueprint || !Blueprint->GeneratedClass || !Blueprint->GeneratedClass->IsChildOf(AVamCharacterActor::StaticClass())) return FReply::Handled();
            const auto* Defaults=Cast<AVamCharacterActor>(Blueprint->GeneratedClass->GetDefaultObject());
            if(!Defaults || Defaults->Character->RuntimeConfiguration.ToSoftObjectPath()!=FSoftObjectPath(Config)) return FReply::Handled();
            FActorSpawnParameters Spawn;Spawn.ObjectFlags=RF_Transient;
            auto* Actor=World->SpawnActor<AVamCharacterActor>(Blueprint->GeneratedClass,FVector::ZeroVector,FRotator::ZeroRotator,Spawn);
            if(!Actor) return FReply::Handled();Actor->LoadCharacter();DebugActor=Actor;SelectedBone=INDEX_NONE;
            GEditor->SelectNone(false,true,false);GEditor->SelectActor(Actor,true,true);
            const TWeakObjectPtr<AVamCharacterActor> PendingActor=Actor;const double Deadline=FPlatformTime::Seconds()+20;
            FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([State,PendingActor,Deadline](float){
                if(!PendingActor.IsValid() || FPlatformTime::Seconds()>Deadline) return false;
                if(!PendingActor->Character->Body) return true;
                if(State->Box.IsValid()){State->Box->ClearChildren();AddControls(State->Box.ToSharedRef(),PendingActor.Get(),State->Filter);}return false;
            }),.5f);
            return FReply::Handled();})]
        +SVerticalBox::Slot().AutoHeight().Padding(8)[SNew(STextBlock).Text_Lambda([](){
            auto* Actor=CurrentActor();auto* Body=Actor?Actor->Character->Body.Get():nullptr;
            if (!Body || SelectedBone==INDEX_NONE || !Body->GetSkeletalMeshAsset()) return FText::FromString(TEXT("橙色 root 控制点移动整个人物；青色控制点旋转关节。"));
            const auto& Skeleton=Body->GetSkeletalMeshAsset()->GetRefSkeleton();
            if (!Skeleton.IsValidIndex(SelectedBone)) return FText::FromString(TEXT("未选中控制点"));
            if (SelectedBone==RootBoneIndex(Actor->Character.Get())) return FText::FromString(TEXT("选中 root · 移动整个人物"));
            return FText::FromString(TEXT("选中旋转关节：")+Skeleton.GetBoneName(SelectedBone).ToString());})]
        +SVerticalBox::Slot().AutoHeight().Padding(8)[SNew(SHorizontalBox).Visibility_Lambda([](){return RootSelected()?EVisibility::Visible:EVisibility::Collapsed;})
            +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(STextBlock).Text(FText::FromString(TEXT("人物世界位置 cm")))]
            +SHorizontalBox::Slot().AutoWidth().Padding(6,0)[SNew(STextBlock).Text(FText::FromString(TEXT("X")))]
            +SHorizontalBox::Slot().FillWidth(1)[SNew(SSpinBox<float>).MinValue(-100000.f).MaxValue(100000.f).Value_Lambda([](){return RootAxis(0);}).OnValueChanged_Lambda([](float Value){SetRootAxis(0,Value);})]
            +SHorizontalBox::Slot().AutoWidth().Padding(6,0)[SNew(STextBlock).Text(FText::FromString(TEXT("Y")))]
            +SHorizontalBox::Slot().FillWidth(1)[SNew(SSpinBox<float>).MinValue(-100000.f).MaxValue(100000.f).Value_Lambda([](){return RootAxis(1);}).OnValueChanged_Lambda([](float Value){SetRootAxis(1,Value);})]
            +SHorizontalBox::Slot().AutoWidth().Padding(6,0)[SNew(STextBlock).Text(FText::FromString(TEXT("Z")))]
            +SHorizontalBox::Slot().FillWidth(1)[SNew(SSpinBox<float>).MinValue(-100000.f).MaxValue(100000.f).Value_Lambda([](){return RootAxis(2);}).OnValueChanged_Lambda([](float Value){SetRootAxis(2,Value);})]]
        +SVerticalBox::Slot().AutoHeight().Padding(8)[SNew(SHorizontalBox).Visibility_Lambda([](){return PoseSelected()?EVisibility::Visible:EVisibility::Collapsed;})
            +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(STextBlock).Text(FText::FromString(TEXT("局部旋转 °")))]
            +SHorizontalBox::Slot().AutoWidth().Padding(6,0)[SNew(STextBlock).Text(FText::FromString(TEXT("X")))]
            +SHorizontalBox::Slot().FillWidth(1)[SNew(SSpinBox<float>).MinValue(-360.f).MaxValue(360.f).Value_Lambda([](){return BoneRotationAxis(0);}).OnValueChanged_Lambda([](float Value){SetBoneRotationAxis(0,Value);})]
            +SHorizontalBox::Slot().AutoWidth().Padding(6,0)[SNew(STextBlock).Text(FText::FromString(TEXT("Y")))]
            +SHorizontalBox::Slot().FillWidth(1)[SNew(SSpinBox<float>).MinValue(-360.f).MaxValue(360.f).Value_Lambda([](){return BoneRotationAxis(1);}).OnValueChanged_Lambda([](float Value){SetBoneRotationAxis(1,Value);})]
            +SHorizontalBox::Slot().AutoWidth().Padding(6,0)[SNew(STextBlock).Text(FText::FromString(TEXT("Z")))]
            +SHorizontalBox::Slot().FillWidth(1)[SNew(SSpinBox<float>).MinValue(-360.f).MaxValue(360.f).Value_Lambda([](){return BoneRotationAxis(2);}).OnValueChanged_Lambda([](float Value){SetBoneRotationAxis(2,Value);})]
            +SHorizontalBox::Slot().AutoWidth().Padding(6,0)[SNew(SButton).Text(FText::FromString(TEXT("重置此关节"))).OnClicked_Lambda([](){if (PoseSelected()) if (auto* Actor=CurrentActor()) Actor->Character->SetPoseControlRotation(SelectedBone,FRotator::ZeroRotator);return FReply::Handled();})]]
        +SVerticalBox::Slot().AutoHeight().Padding(8)[SNew(STextBlock).Text_Lambda([](){
            const auto* A=CurrentActor();if(!A || !A->SoftTissue) return FText::FromString(TEXT("Soft Tissue：未选择人物"));
            const auto* T=A->SoftTissue.Get();const auto O=T->GetBodySurfaceOutput();
            return FText::FromString(FString::Printf(TEXT("Soft Tissue（PIE 人物）：%s | Surface %s | Shape %d | Solver %lld | %s"),
                *StaticEnum<EVamSoftTissueState>()->GetNameStringByValue(int64(T->State)),O.Valid?TEXT("有效"):TEXT("无效"),O.ShapeRevision,O.SolverRevision,*T->LastError));}).AutoWrapText(true)]
        +SVerticalBox::Slot().AutoHeight().Padding(8)[SNew(SHorizontalBox)
            +SHorizontalBox::Slot().AutoWidth()[SNew(SButton).Text(FText::FromString(TEXT("软组织 Off"))).OnClicked_Lambda([](){if(auto* A=CurrentActor()) if(A->SoftTissue) A->SoftTissue->SetSoftTissueEnabled(false);return FReply::Handled();})]
            +SHorizontalBox::Slot().AutoWidth().Padding(4,0)[SNew(SButton).Text(FText::FromString(TEXT("Balanced"))).OnClicked_Lambda([](){if(auto* A=CurrentActor()) if(A->SoftTissue) A->SoftTissue->SetSoftTissueQuality(EVamSoftTissueQuality::Balanced);return FReply::Handled();})]
            +SHorizontalBox::Slot().AutoWidth().Padding(4,0)[SNew(SButton).Text(FText::FromString(TEXT("High"))).OnClicked_Lambda([](){if(auto* A=CurrentActor()) if(A->SoftTissue) A->SoftTissue->SetSoftTissueQuality(EVamSoftTissueQuality::High);return FReply::Handled();})]
            +SHorizontalBox::Slot().AutoWidth().Padding(4,0)[SNew(SButton).Text(FText::FromString(TEXT("重建软组织 rest"))).OnClicked_Lambda([](){if(auto* A=CurrentActor()) if(A->SoftTissue) A->SoftTissue->ResetSoftTissue();return FReply::Handled();})]]
        +SVerticalBox::Slot().AutoHeight().Padding(8)[SNew(STextBlock).Text(FText::FromString(TEXT("Stage06 · 运行时与惯性见证")))]
        +SVerticalBox::Slot().AutoHeight().Padding(8)[SNew(SHorizontalBox)
            +SHorizontalBox::Slot().AutoWidth()[SNew(SButton).Text(FText::FromString(TEXT("暂停/继续见证时钟"))).OnClicked_Lambda([](){if(auto* A=CurrentActor()) if(A->Motion) A->Motion->SetPreviewPaused(!A->Motion->GetClock().bPaused);return FReply::Handled();})]
            +SHorizontalBox::Slot().AutoWidth().Padding(4,0)[SNew(SButton).Text(FText::FromString(TEXT("单步见证"))).OnClicked_Lambda([](){if(auto* A=CurrentActor()) if(A->Motion) A->Motion->StepPreview();return FReply::Handled();})]
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
        +SVerticalBox::Slot().AutoHeight().Padding(8)[SNew(STextBlock).Text(FText::FromString(TEXT("橙色 root 点仅平移整个人物；青色点仅旋转四肢、躯干、头部与手指关节，姿态角度由 Rig 限位。悬停时控制点放大并显示白色中心，按住/拖动时变为黄色中心和红色或紫色光环。PIE / Simulate 中 root 移动注入速度和惯性，普通编辑器移动不产生模拟物理。黄色见证区域不代表全身软体。P/O/R 只暂停、单步、重置见证；Chaos 和动画继续运行。拖动 root 会切到局部物理响应；青色摆姿点不是物理抓取点。"))).AutoWrapText(true)]
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

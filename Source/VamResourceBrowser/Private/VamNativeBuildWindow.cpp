#include "VamNativeBuildWindow.h"
#include "CoreMinimal.h"
#include "Interfaces/IPluginManager.h"
#include "Widgets/SWindow.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformProcess.h"
#include "HAL/FileManager.h"
#include "Containers/Ticker.h"
#include "Serialization/JsonSerializer.h"
#include "DesktopPlatformModule.h"
#include "IDesktopPlatform.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"

namespace
{
struct FNativeJobUI
{
    FProcHandle Process;
    FString Directory, Target, Status=TEXT("选择目标 Content 路径与可编辑 Morph 集。重导入保护用户修改，不覆盖冲突资产。");
    bool Cancellable=false;
    TOptional<float> Fraction=0.f;
    ~FNativeJobUI() { if (Process.IsValid()) FPlatformProcess::CloseProc(Process); }
    void Cancel() { if (Cancellable) FFileHelper::SaveStringToFile(TEXT("cancel"),*(Directory/TEXT("cancel"))); }
};
TWeakPtr<SWindow> ExistingWindow;
}

void ShowVamNativeBuildWindow(bool UpgradeExisting)
{
    if (auto Existing=ExistingWindow.Pin()) { Existing->BringToFront(); return; }
    const FString Plugin=FPaths::ConvertRelativePathToFull(IPluginManager::Get().FindPlugin(TEXT("VamResourceBrowser"))->GetBaseDir());
    auto State=MakeShared<FNativeJobUI>();
    TSharedPtr<SEditableTextBox> Target,MorphSet;
    FString InitialTarget=TEXT("/Game/VamCharacters");
    if (UpgradeExisting) {
        TArray<FAssetData> Selected;FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser")).Get().GetSelectedAssets(Selected);
        InitialTarget=Selected.Num()==1 ? Selected[0].GetObjectPathString() : TEXT("");
        State->Status=TEXT("选择已保存的原生 BP_VamCharacter 或 CD_Character；生成新的 Runtime BP，保留原资产和场景实例。仅支持已验证的骨架家族。当前胸/腿代理质量与性能仍待完善。");
    }
    auto Window=SNew(SWindow).Title(FText::FromString(UpgradeExisting ? TEXT("升级人物 Runtime / 软组织") : TEXT("导入人物与 Runtime / 软组织"))).ClientSize(FVector2D(740,320));
    ExistingWindow=Window;
    Window->SetContent(SNew(SVerticalBox)
        +SVerticalBox::Slot().AutoHeight().Padding(12)[SNew(STextBlock).Text(FText::FromString(UpgradeExisting ? TEXT("人物 BP 或 CharacterDefinition 资产路径（从内容浏览器选中后打开）") : TEXT("目标目录（项目 Content 路径；完成后另生成 Runtime BP）")))]
        +SVerticalBox::Slot().AutoHeight().Padding(12,0)[SAssignNew(Target,SEditableTextBox).Text(FText::FromString(InitialTarget))]
        +SVerticalBox::Slot().AutoHeight().Padding(12)[SNew(STextBlock).Text(FText::FromString(UpgradeExisting ? TEXT("升级复用原人物 Morph 集；不重新导入来源文件") : TEXT("可编辑 Morph 集 JSON：显式选择，包含零初始权重参数")))]
        +SVerticalBox::Slot().AutoHeight().Padding(12,0)[SNew(SHorizontalBox)
            +SHorizontalBox::Slot().FillWidth(1)[SAssignNew(MorphSet,SEditableTextBox).IsEnabled(!UpgradeExisting).Text(FText::FromString(Plugin/TEXT("Config/Stage05MorphSet.json")))]
            +SHorizontalBox::Slot().AutoWidth()[SNew(SButton).IsEnabled(!UpgradeExisting).Text(FText::FromString(TEXT("选择文件"))).OnClicked_Lambda([MorphSet]() {
                TArray<FString> Files;auto* Platform=FDesktopPlatformModule::Get();
                if (Platform && Platform->OpenFileDialog(nullptr,TEXT("选择 Morph 集"),TEXT(""),TEXT(""),TEXT("JSON|*.json"),0,Files) && Files.Num()) MorphSet->SetText(FText::FromString(Files[0]));
                return FReply::Handled(); })]]
        +SVerticalBox::Slot().AutoHeight().Padding(12)[SNew(SHorizontalBox)
            +SHorizontalBox::Slot().AutoWidth()[SNew(SButton).Text(FText::FromString(UpgradeExisting ? TEXT("升级并生成 Runtime BP") : TEXT("开始导入与 Runtime 构建"))).IsEnabled_Lambda([State](){return !State->Process.IsValid();}).OnClicked_Lambda([State,Target,MorphSet,Plugin,UpgradeExisting]() {
                FString Raw;TSharedPtr<FJsonObject> Preview;
                if (!UpgradeExisting && (!FFileHelper::LoadFileToString(Raw,*(Plugin/TEXT("Saved/Decoded/latest.json"))) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Raw),Preview)))
                {State->Status=TEXT("请先解码人物并解析来源材质。");return FReply::Handled();}
                const FString PreviewPath=UpgradeExisting ? FString() : Plugin/TEXT("Saved/Decoded")/(Preview->GetStringField(TEXT("decode_id"))+TEXT(".appearance.preview.json"));
                if (!UpgradeExisting && (!FPaths::FileExists(PreviewPath) || !FPaths::FileExists(MorphSet->GetText().ToString())))
                {State->Status=TEXT("缺少来源材质预览或 Morph 集文件。");return FReply::Handled();}
                State->Target=UpgradeExisting ? TEXT("/Game/VamRuntime") : Target->GetText().ToString();
                if (UpgradeExisting && !Target->GetText().ToString().StartsWith(TEXT("/Game/"))) {State->Status=TEXT("请填写已保存的 /Game 人物资产路径。");return FReply::Handled();}
                State->Directory=Plugin/TEXT("Saved/NativeBuild/Jobs")/FGuid::NewGuid().ToString(EGuidFormats::Digits);
                IFileManager::Get().MakeDirectory(*State->Directory,true);
                auto Request=MakeShared<FJsonObject>();Request->SetStringField(TEXT("target"),State->Target);
                Request->SetStringField(TEXT("morph_set"),FPaths::ConvertRelativePathToFull(MorphSet->GetText().ToString()));Request->SetStringField(TEXT("preview"),PreviewPath);
                if (UpgradeExisting) Request->SetStringField(TEXT("upgrade_asset"),Target->GetText().ToString());
                FString Json;FJsonSerializer::Serialize(Request,TJsonWriterFactory<>::Create(&Json));FFileHelper::SaveStringToFile(Json,*(State->Directory/TEXT("request.json")));
                FString Args=FString::Printf(TEXT("\"%s\" -run=pythonscript -script=\"%s\" -VamJob=\"%s\" -NullRHI -unattended -nosplash -abslog=\"%s\""),
                    *FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath()),*(Plugin/TEXT("Scripts/ue_native_job.py")),*State->Directory,*(State->Directory/TEXT("build.log")));
                State->Process=FPlatformProcess::CreateProc(*(FPaths::EngineDir()/TEXT("Binaries/Win64/UnrealEditor-Cmd.exe")),*Args,true,true,true,nullptr,0,nullptr,nullptr);
                State->Cancellable=State->Process.IsValid();State->Fraction.Reset();State->Status=State->Cancellable ? TEXT("准备构建…") : TEXT("无法启动构建进程");
                if (State->Process.IsValid()) FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([State](float){
                    FString StatusJson;TSharedPtr<FJsonObject> Info;
                    if (FFileHelper::LoadFileToString(StatusJson,*(State->Directory/TEXT("status.json"))) && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(StatusJson),Info))
                    {
                        State->Status=Info->GetStringField(TEXT("phase"))+TEXT(": ")+Info->GetStringField(TEXT("detail"));State->Cancellable=Info->GetBoolField(TEXT("cancellable"));
                        double Done=0,Total=0;
                        if (Info->TryGetNumberField(TEXT("done"),Done) && Info->TryGetNumberField(TEXT("total"),Total) && Total>0)
                        {State->Fraction=FMath::Clamp(static_cast<float>(Done/Total),0.f,1.f);State->Status+=FString::Printf(TEXT(" (%d/%d)"),static_cast<int32>(Done),static_cast<int32>(Total));}
                        else State->Fraction.Reset();
                    }
                    if (FPlatformProcess::IsProcRunning(State->Process)) return true;
                    int32 Code=0;FPlatformProcess::GetProcReturnCode(State->Process,&Code);FPlatformProcess::CloseProc(State->Process);State->Process.Reset();State->Cancellable=false;
                    if (Code!=0) State->Status+=TEXT("\n构建失败，日志：")+State->Directory;
                    else FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get().ScanPathsSynchronous({State->Target,TEXT("/Game/VamRuntime")},true);
                    State->Fraction=Code==0 ? TOptional<float>(1.f) : TOptional<float>(0.f);
                    return false;
                }),.5f);
                return FReply::Handled(); })]
            +SHorizontalBox::Slot().AutoWidth().Padding(12,0)[SNew(SButton).Text(FText::FromString(TEXT("取消"))).IsEnabled_Lambda([State](){return State->Cancellable;}).OnClicked_Lambda([State](){State->Cancel();return FReply::Handled();})]]
        +SVerticalBox::Slot().AutoHeight().Padding(12,4)[SNew(SProgressBar).Percent_Lambda([State](){return State->Fraction;})]
        +SVerticalBox::Slot().FillHeight(1).Padding(12)[SNew(SScrollBox)+SScrollBox::Slot()[SNew(STextBlock).AutoWrapText(true).Text_Lambda([State](){return FText::FromString(State->Status);})]]);
    Window->SetOnWindowClosed(FOnWindowClosed::CreateLambda([State](const TSharedRef<SWindow>&){State->Cancel();}));
    FSlateApplication::Get().AddWindow(Window);
}

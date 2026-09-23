#include "VamNativeBuildWindow.h"
#include "CoreMinimal.h"
#include "Interfaces/IPluginManager.h"
#include "Widgets/SWindow.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
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

namespace
{
struct FNativeJobUI
{
    FProcHandle Process;
    FString Directory, Target, Status=TEXT("选择目标 Content 路径与可编辑 Morph 集。重导入保护用户修改，不覆盖冲突资产。");
    bool Cancellable=false;
    ~FNativeJobUI() { if (Process.IsValid()) FPlatformProcess::CloseProc(Process); }
    void Cancel() { if (Cancellable) FFileHelper::SaveStringToFile(TEXT("cancel"),*(Directory/TEXT("cancel"))); }
};
TWeakPtr<SWindow> ExistingWindow;
}

void ShowVamNativeBuildWindow()
{
    if (auto Existing=ExistingWindow.Pin()) { Existing->BringToFront(); return; }
    const FString Plugin=FPaths::ConvertRelativePathToFull(IPluginManager::Get().FindPlugin(TEXT("VamResourceBrowser"))->GetBaseDir());
    auto State=MakeShared<FNativeJobUI>();
    TSharedPtr<SEditableTextBox> Target,MorphSet;
    auto Window=SNew(SWindow).Title(FText::FromString(TEXT("构建正式人物资产"))).ClientSize(FVector2D(740,290));
    ExistingWindow=Window;
    Window->SetContent(SNew(SVerticalBox)
        +SVerticalBox::Slot().AutoHeight().Padding(12)[SNew(STextBlock).Text(FText::FromString(TEXT("目标目录（项目 Content 路径）")))]
        +SVerticalBox::Slot().AutoHeight().Padding(12,0)[SAssignNew(Target,SEditableTextBox).Text(FText::FromString(TEXT("/Game/VamCharacters")))]
        +SVerticalBox::Slot().AutoHeight().Padding(12)[SNew(STextBlock).Text(FText::FromString(TEXT("可编辑 Morph 集 JSON：显式选择，包含零初始权重参数")))]
        +SVerticalBox::Slot().AutoHeight().Padding(12,0)[SNew(SHorizontalBox)
            +SHorizontalBox::Slot().FillWidth(1)[SAssignNew(MorphSet,SEditableTextBox).Text(FText::FromString(Plugin/TEXT("Config/Stage05MorphSet.json")))]
            +SHorizontalBox::Slot().AutoWidth()[SNew(SButton).Text(FText::FromString(TEXT("选择文件"))).OnClicked_Lambda([MorphSet]() {
                TArray<FString> Files;auto* Platform=FDesktopPlatformModule::Get();
                if (Platform && Platform->OpenFileDialog(nullptr,TEXT("选择 Morph 集"),TEXT(""),TEXT(""),TEXT("JSON|*.json"),0,Files) && Files.Num()) MorphSet->SetText(FText::FromString(Files[0]));
                return FReply::Handled(); })]]
        +SVerticalBox::Slot().AutoHeight().Padding(12)[SNew(SHorizontalBox)
            +SHorizontalBox::Slot().AutoWidth()[SNew(SButton).Text(FText::FromString(TEXT("开始构建 / 重导入"))).IsEnabled_Lambda([State](){return !State->Process.IsValid();}).OnClicked_Lambda([State,Target,MorphSet,Plugin]() {
                FString Raw;TSharedPtr<FJsonObject> Preview;
                if (!FFileHelper::LoadFileToString(Raw,*(Plugin/TEXT("Saved/Decoded/latest.json"))) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Raw),Preview))
                {State->Status=TEXT("请先解码人物并解析来源材质。");return FReply::Handled();}
                const FString PreviewPath=Plugin/TEXT("Saved/Decoded")/(Preview->GetStringField(TEXT("decode_id"))+TEXT(".appearance.preview.json"));
                if (!FPaths::FileExists(PreviewPath) || !FPaths::FileExists(MorphSet->GetText().ToString()))
                {State->Status=TEXT("缺少来源材质预览或 Morph 集文件。");return FReply::Handled();}
                State->Target=Target->GetText().ToString();
                State->Directory=Plugin/TEXT("Saved/NativeBuild/Jobs")/FGuid::NewGuid().ToString(EGuidFormats::Digits);
                IFileManager::Get().MakeDirectory(*State->Directory,true);
                auto Request=MakeShared<FJsonObject>();Request->SetStringField(TEXT("target"),State->Target);
                Request->SetStringField(TEXT("morph_set"),FPaths::ConvertRelativePathToFull(MorphSet->GetText().ToString()));Request->SetStringField(TEXT("preview"),PreviewPath);
                FString Json;FJsonSerializer::Serialize(Request,TJsonWriterFactory<>::Create(&Json));FFileHelper::SaveStringToFile(Json,*(State->Directory/TEXT("request.json")));
                FString Args=FString::Printf(TEXT("\"%s\" -run=pythonscript -script=\"%s\" -VamJob=\"%s\" -NullRHI -unattended -nosplash -abslog=\"%s\""),
                    *FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath()),*(Plugin/TEXT("Scripts/ue_native_job.py")),*State->Directory,*(State->Directory/TEXT("build.log")));
                State->Process=FPlatformProcess::CreateProc(*(FPaths::EngineDir()/TEXT("Binaries/Win64/UnrealEditor-Cmd.exe")),*Args,true,true,true,nullptr,0,nullptr,nullptr);
                State->Cancellable=State->Process.IsValid();State->Status=State->Cancellable ? TEXT("准备构建…") : TEXT("无法启动构建进程");
                if (State->Process.IsValid()) FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([State](float){
                    FString StatusJson;TSharedPtr<FJsonObject> Info;
                    if (FFileHelper::LoadFileToString(StatusJson,*(State->Directory/TEXT("status.json"))) && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(StatusJson),Info))
                    {State->Status=Info->GetStringField(TEXT("phase"))+TEXT(": ")+Info->GetStringField(TEXT("detail"));State->Cancellable=Info->GetBoolField(TEXT("cancellable"));}
                    if (FPlatformProcess::IsProcRunning(State->Process)) return true;
                    int32 Code=0;FPlatformProcess::GetProcReturnCode(State->Process,&Code);FPlatformProcess::CloseProc(State->Process);State->Process.Reset();State->Cancellable=false;
                    if (Code!=0) State->Status+=TEXT("\n构建失败，日志：")+State->Directory;
                    else FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get().ScanPathsSynchronous({State->Target},true);
                    return false;
                }),.5f);
                return FReply::Handled(); })]
            +SHorizontalBox::Slot().AutoWidth().Padding(12,0)[SNew(SButton).Text(FText::FromString(TEXT("取消"))).IsEnabled_Lambda([State](){return State->Cancellable;}).OnClicked_Lambda([State](){State->Cancel();return FReply::Handled();})]]
        +SVerticalBox::Slot().FillHeight(1).Padding(12)[SNew(SScrollBox)+SScrollBox::Slot()[SNew(STextBlock).AutoWrapText(true).Text_Lambda([State](){return FText::FromString(State->Status);})]]);
    Window->SetOnWindowClosed(FOnWindowClosed::CreateLambda([State](const TSharedRef<SWindow>&){State->Cancel();}));
    FSlateApplication::Get().AddWindow(Window);
}

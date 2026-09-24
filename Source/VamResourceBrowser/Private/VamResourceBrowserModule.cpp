#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "Interfaces/IPluginManager.h"
#include "ToolMenus.h"
#include "SWebBrowser.h"
#include "WebBrowserModule.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Framework/Docking/TabManager.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/PlatformProcess.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Containers/Ticker.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "DesktopPlatformModule.h"
#include "IDesktopPlatform.h"
#include "IPythonScriptPlugin.h"
#include "VamNativeBuildWindow.h"
#include "VamDebugPanel.h"

#define LOCTEXT_NAMESPACE "VamResourceBrowser"

class FVamResourceBrowserModule final : public IModuleInterface
{
    FProcHandle Worker;
    FString DataDir, ReadyPath, Url;
    TWeakPtr<SWebBrowser> Browser;
    FTSTicker::FDelegateHandle PollHandle;
    FTSTicker::FDelegateHandle PreviewHandle;
    FDelegateHandle MenuHandle;
    double StartTime = 0;

    bool PreviewTick(float)
    {
        if (DataDir.IsEmpty()) return true;
        FFileHelper::SaveStringToFile(FString::FromInt(FPlatformProcess::GetCurrentProcessId()), *(DataDir / TEXT("editor-alive.txt")));
        const FString Queue = DataDir / TEXT("preview-request.json");
        if (!FPaths::FileExists(Queue)) return true;
        IFileManager::Get().Move(*(DataDir / TEXT("preview-active.json")), *Queue, true);
        const FString Script = FPaths::ConvertRelativePathToFull(IPluginManager::Get().FindPlugin(TEXT("VamResourceBrowser"))->GetBaseDir() / TEXT("Scripts/ue_current_preview.py"));
        if (auto* Python = IPythonScriptPlugin::Get()) Python->ExecPythonCommand(*Script);
        return true;
    }

    bool Poll(float)
    {
        FString Text;
        TSharedPtr<FJsonObject> Ready;
        if (FFileHelper::LoadFileToString(Text, *ReadyPath) &&
            FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Ready) && Ready.IsValid())
        {
            Url = Ready->GetStringField(TEXT("url"));
            if (auto View = Browser.Pin()) View->LoadURL(Url);
            PollHandle.Reset();
            return false;
        }
        if (!FPlatformProcess::IsProcRunning(Worker) || FPlatformTime::Seconds() - StartTime > 30)
        {
            if (auto View = Browser.Pin()) View->LoadString(
                TEXT("<body style='background:#202226;color:#eee;font:16px sans-serif;padding:32px'>VaM browser service failed to start. See Plugins/VamResourceBrowser/Saved/service.log. Close and reopen this tab to retry.</body>"), TEXT("about:blank"));
            PollHandle.Reset();
            return false;
        }
        return true;
    }

    void StopWorker()
    {
        if (PollHandle.IsValid()) { FTSTicker::GetCoreTicker().RemoveTicker(PollHandle); PollHandle.Reset(); }
        if (PreviewHandle.IsValid()) { FTSTicker::GetCoreTicker().RemoveTicker(PreviewHandle); PreviewHandle.Reset(); }
        if (!DataDir.IsEmpty()) IFileManager::Get().Delete(*(DataDir / TEXT("editor-alive.txt")));
        if (Worker.IsValid())
        {
            FPlatformProcess::TerminateProc(Worker, true);
            FPlatformProcess::CloseProc(Worker);
            Worker.Reset();
        }
        Url.Empty();
    }

    bool StartWorker()
    {
        StopWorker();
        const FString PluginDir = IPluginManager::Get().FindPlugin(TEXT("VamResourceBrowser"))->GetBaseDir();
        DataDir = FPaths::ConvertRelativePathToFull(PluginDir / TEXT("Saved"));
        IFileManager::Get().MakeDirectory(*DataDir, true);
        ReadyPath = DataDir / TEXT("ready.json");
        PreviewHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FVamResourceBrowserModule::PreviewTick), 1.0f);
        FString ExistingText;
        TSharedPtr<FJsonObject> Existing;
        if (FFileHelper::LoadFileToString(ExistingText, *ReadyPath) && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(ExistingText), Existing) && Existing.IsValid() && FPlatformProcess::IsApplicationRunning((uint32)Existing->GetIntegerField(TEXT("pid"))))
        {
            Url = Existing->GetStringField(TEXT("url"));
            if (auto View = Browser.Pin()) View->LoadURL(Url);
            return true;
        }
        IFileManager::Get().Delete(*ReadyPath);
        const FString Python = FPaths::ConvertRelativePathToFull(FPaths::EngineDir() / TEXT("Binaries/ThirdParty/Python3/Win64/python.exe"));
        const FString Script = FPaths::ConvertRelativePathToFull(PluginDir / TEXT("Scripts/vam_index.py"));
        if (!FPaths::FileExists(Python)) return false;
        const FString Args = FString::Printf(TEXT("-I \"%s\" --serve --data \"%s\" --parent %u"), *Script, *DataDir, FPlatformProcess::GetCurrentProcessId());
        Worker = FPlatformProcess::CreateProc(*Python, *Args, false, true, true, nullptr, 0, *PluginDir, nullptr);
        if (!Worker.IsValid()) return false;
        StartTime = FPlatformTime::Seconds();
        PollHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FVamResourceBrowserModule::Poll), 0.25f);
        return true;
    }

    TSharedRef<SDockTab> Spawn(const FSpawnTabArgs&)
    {
        // UE 5.8 SWebBrowserView checks IsAvailable without loading this module.
        // Linking WebBrowser alone does not run its StartupModule (CEF initialization).
        auto& WebModule = IWebBrowserModule::Get();
        if (!WebModule.IsWebModuleAvailable())
        {
            UE_LOG(LogTemp, Error, TEXT("VaM browser: UE WebBrowser/CEF initialization failed"));
            return SNew(SDockTab).TabRole(ETabRole::NomadTab)
                [ SNew(STextBlock).Text(LOCTEXT("BrowserUnavailable", "UE 内嵌浏览器初始化失败。请检查引擎 CEF 文件与日志；关闭此页后可重试。")) ];
        }
        TSharedPtr<SWebBrowser> View;
        TSharedRef<SDockTab> Tab = SNew(SDockTab).TabRole(ETabRole::NomadTab)
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(6, 4)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth()
                [ SNew(SButton).Text(LOCTEXT("Choose", "选择 VaM 目录…"))
                    .OnClicked_Lambda([this]() {
                        FString Folder;
                        auto* Platform = FDesktopPlatformModule::Get();
                        if (Platform && Platform->OpenDirectoryDialog(FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr), TEXT("选择 VaM 根目录"), TEXT("F:/Virt A Mate"), Folder))
                        {
                            // JSON serialization provides JavaScript string escaping, including Unicode.
                            TArray<TSharedPtr<FJsonValue>> Values;
                            Values.Add(MakeShared<FJsonValueString>(Folder));
                            FString Json;
                            FJsonSerializer::Serialize(Values, TJsonWriterFactory<>::Create(&Json));
                            if (auto Web = Browser.Pin()) Web->ExecuteJavascript(TEXT("window.setVamRoot && window.setVamRoot(") + Json + TEXT("[0]);"));
                        }
                        return FReply::Handled();
                    }) ]
                + SHorizontalBox::Slot().AutoWidth().Padding(8,0)
                [ SNew(SButton).Text(LOCTEXT("BuildNative", "生成 UE 人物资产"))
                    .ToolTipText(LOCTEXT("BuildNativeTip", "选择目标目录与可编辑 Morph 集；支持进度、取消与受保护的重导入"))
                    .OnClicked_Lambda([]() {
                        ShowVamNativeBuildWindow();
                        return FReply::Handled();
                    }) ]
                + SHorizontalBox::Slot().AutoWidth().Padding(8,0)
                [ SNew(SButton).Text(LOCTEXT("CharacterDebug", "人物调试"))
                    .ToolTipText(LOCTEXT("CharacterDebugTip", "在场景人物上显示并拖动骨骼控制点，预览 Morph 与能力状态"))
                    .OnClicked_Lambda([](){OpenVamDebugPanel();return FReply::Handled();}) ]
                + SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center).Padding(12,0)
                [ SNew(STextBlock).Text(LOCTEXT("ReadOnly", "VaM 资源浏览器 · 在当前编辑器场景预览 · 缓存位于插件 Saved 目录")) ]
            ]
            + SVerticalBox::Slot().FillHeight(1)
            [ SAssignNew(View, SWebBrowser).InitialURL(TEXT("about:blank")).ShowControls(false)
                .ShowAddressBar(false).BrowserFrameRate(30).BackgroundColor(FColor(24,26,30))
                .OnBeforePopup_Lambda([](FString, FString) { return true; })
                .OnBeforeNavigation_Lambda([this](const FString& NewUrl, const FWebNavigationRequest&) {
                    return !NewUrl.StartsWith(TEXT("about:blank")) && (Url.IsEmpty() || !NewUrl.StartsWith(Url));
                }) ]
        ];
        Browser = View;
        View->LoadString(TEXT("<body style='background:#181a1e;color:#ddd;font:16px sans-serif;padding:32px'>正在启动 VaM 资源索引服务…</body>"), TEXT("about:blank"));
        if (!StartWorker()) View->LoadString(TEXT("<body>Bundled UE Python runtime not found. Install the engine Python runtime component.</body>"), TEXT("about:blank"));
        Tab->SetOnTabClosed(SDockTab::FOnTabClosedCallback::CreateLambda([this](TSharedRef<SDockTab>) { Browser.Reset(); StopWorker(); }));
        return Tab;
    }

public:
    virtual void StartupModule() override
    {
        RegisterVamDebugPanel();
        FGlobalTabmanager::Get()->RegisterNomadTabSpawner(TEXT("VamResourceBrowser"), FOnSpawnTab::CreateRaw(this, &FVamResourceBrowserModule::Spawn))
            .SetDisplayName(LOCTEXT("TabName", "VaM 资源浏览器")).SetMenuType(ETabSpawnerMenuType::Hidden);
        MenuHandle = UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateLambda([this]() {
            FToolMenuOwnerScoped Owner(this);
            UToolMenu* Menu = UToolMenus::Get()->ExtendMenu(TEXT("LevelEditor.MainMenu.Window"));
            Menu->FindOrAddSection(TEXT("WindowLayout")).AddMenuEntry(TEXT("VamResourceBrowser"),
                LOCTEXT("Menu", "VaM 资源浏览器"), LOCTEXT("Tip", "浏览 VaM 资源并在当前场景预览人物"),
                FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([]() { FGlobalTabmanager::Get()->TryInvokeTab(FTabId(FName(TEXT("VamResourceBrowser")))); })));
            Menu->FindOrAddSection(TEXT("WindowLayout")).AddMenuEntry(TEXT("VamCharacterDebug"),
                LOCTEXT("DebugMenu", "VaM 人物调试"), LOCTEXT("DebugMenuTip", "打开场景人物骨骼和 Morph 控制点调试面板"),
                FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([](){OpenVamDebugPanel();})));
        }));
    }
    virtual void ShutdownModule() override
    {
        if (TSharedPtr<SDockTab> Tab = FGlobalTabmanager::Get()->FindExistingLiveTab(FTabId(TEXT("VamResourceBrowser")))) Tab->RequestCloseTab();
        StopWorker();
        UToolMenus::UnRegisterStartupCallback(MenuHandle);
        UToolMenus::UnregisterOwner(this);
        UnregisterVamDebugPanel();
        FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(TEXT("VamResourceBrowser"));
    }
};

IMPLEMENT_MODULE(FVamResourceBrowserModule, VamResourceBrowser)
#undef LOCTEXT_NAMESPACE

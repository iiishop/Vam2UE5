#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "Interfaces/IPluginManager.h"
#include "ToolMenus.h"
#include "SWebBrowser.h"
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

#define LOCTEXT_NAMESPACE "VamResourceBrowser"

class FVamResourceBrowserModule final : public IModuleInterface
{
    FProcHandle Worker;
    FString DataDir, ReadyPath, Url;
    TWeakPtr<SWebBrowser> Browser;
    FTSTicker::FDelegateHandle PollHandle;
    FDelegateHandle MenuHandle;
    double StartTime = 0;

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
                + SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center).Padding(12,0)
                [ SNew(STextBlock).Text(LOCTEXT("ReadOnly", "阶段 01–02 · 资源浏览与导入计划 · 数据保存在本插件 Saved 目录")) ]
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
        FGlobalTabmanager::Get()->RegisterNomadTabSpawner(TEXT("VamResourceBrowser"), FOnSpawnTab::CreateRaw(this, &FVamResourceBrowserModule::Spawn))
            .SetDisplayName(LOCTEXT("TabName", "VaM 资源浏览器")).SetMenuType(ETabSpawnerMenuType::Hidden);
        MenuHandle = UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateLambda([this]() {
            FToolMenuOwnerScoped Owner(this);
            UToolMenu* Menu = UToolMenus::Get()->ExtendMenu(TEXT("LevelEditor.MainMenu.Window"));
            Menu->FindOrAddSection(TEXT("WindowLayout")).AddMenuEntry(TEXT("VamResourceBrowser"),
                LOCTEXT("Menu", "VaM 资源浏览器"), LOCTEXT("Tip", "浏览 VaM 松散资源与 VAR 包，不导入几何"),
                FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([]() { FGlobalTabmanager::Get()->TryInvokeTab(TEXT("VamResourceBrowser")); })));
        }));
    }
    virtual void ShutdownModule() override
    {
        if (TSharedPtr<SDockTab> Tab = FGlobalTabmanager::Get()->FindExistingLiveTab(FTabId(TEXT("VamResourceBrowser")))) Tab->RequestCloseTab();
        StopWorker();
        UToolMenus::UnRegisterStartupCallback(MenuHandle);
        UToolMenus::UnregisterOwner(this);
        FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(TEXT("VamResourceBrowser"));
    }
};

IMPLEMENT_MODULE(FVamResourceBrowserModule, VamResourceBrowser)
#undef LOCTEXT_NAMESPACE

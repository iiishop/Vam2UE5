#include "Modules/ModuleManager.h"
#include "Interfaces/IPluginManager.h"
#include "ShaderCore.h"
#include "Misc/Paths.h"
#include "Engine/World.h"
#include "VamGPUContact.h"
class FVamContactGPUModule : public IModuleInterface
{
 FDelegateHandle TickHandle;
 void ShutdownModule() override{FWorldDelegates::OnWorldPostActorTick.Remove(TickHandle);VamGPUShutdown();}
 void StartupModule() override
 {
  const auto Plugin=IPluginManager::Get().FindPlugin(TEXT("VamResourceBrowser"));
  check(Plugin.IsValid());
  TickHandle=FWorldDelegates::OnWorldPostActorTick.AddLambda([](UWorld*,ELevelTick,float){VamGPUFlushBatch();});
  AddShaderSourceDirectoryMapping(TEXT("/Plugin/VamContactGPU"),FPaths::Combine(Plugin->GetBaseDir(),TEXT("Shaders")));
 }
};
IMPLEMENT_MODULE(FVamContactGPUModule,VamContactGPU)

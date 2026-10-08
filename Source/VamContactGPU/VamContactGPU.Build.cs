using UnrealBuildTool;
public class VamContactGPU : ModuleRules
{
 public VamContactGPU(ReadOnlyTargetRules Target):base(Target)
 {
  PCHUsage=PCHUsageMode.UseExplicitOrSharedPCHs;
  PublicDependencyModuleNames.AddRange(new[]{"Core","CoreUObject","Engine","OptimusCore","ComputeFramework"});
  PrivateDependencyModuleNames.AddRange(new[]{"CoreUObject","Engine","Projects","RHI","RenderCore"});
 }
}

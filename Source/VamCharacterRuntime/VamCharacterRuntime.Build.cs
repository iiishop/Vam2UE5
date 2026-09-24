using UnrealBuildTool;
public class VamCharacterRuntime : ModuleRules
{
    public VamCharacterRuntime(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "PhysicsCore", "InputCore" });
        PrivateDependencyModuleNames.AddRange(new[] { "PBIK" });
    }
}

using UnrealBuildTool;
public class VamCharacterRuntime : ModuleRules
{
    public VamCharacterRuntime(ReadOnlyTargetRules Target) : base(Target)
    {
        if (Target.bBuildEditor) PrivateDependencyModuleNames.Add("UnrealEd");
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PrivateDependencyModuleNames.Add("Renderer");
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "PhysicsCore", "InputCore", "ChaosFleshEngine", "ChaosFlesh", "VamContactGPU", "HairStrandsCore", "Niagara" });
        PrivateDependencyModuleNames.AddRange(new[] { "RenderCore", "RHI", "ComputeFramework", "PBIK", "Json", "Chaos", "ChaosCore" });
    }
}

using UnrealBuildTool;

public class VamResourceBrowser : ModuleRules
{
    public VamResourceBrowser(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PrivateDependencyModuleNames.Add("Renderer");
        PrivateDependencyModuleNames.AddRange(new[] {
            "Core", "CoreUObject", "Engine", "Slate", "SlateCore", "UnrealEd",
            "ToolMenus", "Projects", "WebBrowser", "Json", "DesktopPlatform", "PythonScriptPlugin", "ContentBrowser",
            "VamCharacterRuntime", "AssetRegistry", "MeshDescription", "StaticMeshDescription",
            "SkeletalMeshDescription", "ModelingComponentsEditorOnly", "AnimationCore", "GeometryCore", "PhysicsUtilities", "InputCore",
            "VamContactGPU", "Chaos", "RHI", "RenderCore", "OptimusCore", "ComputeFramework", "GeometryAlgorithms", "TetMeshing", "HairStrandsCore", "Niagara", "NiagaraEditor"
        });
    }
}

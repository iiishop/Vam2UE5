using UnrealBuildTool;

public class VamResourceBrowser : ModuleRules
{
    public VamResourceBrowser(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PrivateDependencyModuleNames.AddRange(new[] {
            "Core", "CoreUObject", "Engine", "Slate", "SlateCore", "UnrealEd",
            "ToolMenus", "Projects", "WebBrowser", "Json", "DesktopPlatform", "PythonScriptPlugin",
            "VamCharacterRuntime", "AssetRegistry", "MeshDescription", "StaticMeshDescription",
            "SkeletalMeshDescription", "ModelingComponentsEditorOnly", "AnimationCore", "GeometryCore", "PhysicsUtilities"
        });
    }
}

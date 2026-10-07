using UnrealBuildTool;
public class CrossoverRebuilt : ModuleRules {
    public CrossoverRebuilt(ReadOnlyTargetRules Target) : base(Target) {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        bEnableExceptions = false;
        PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore" });
        PrivateDependencyModuleNames.AddRange(new string[] {
            "Sockets", "Networking", "Json", "JsonUtilities", "ProceduralMeshComponent",
            "Slate", "SlateCore", "ImageWrapper", "RenderCore", "RHI", "ApplicationCore" });
    }
}

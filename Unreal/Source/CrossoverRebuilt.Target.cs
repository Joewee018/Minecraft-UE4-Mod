using UnrealBuildTool;
public class CrossoverRebuiltTarget : TargetRules {
    public CrossoverRebuiltTarget(TargetInfo Target) : base(Target) {
        Type = TargetType.Game;
        DefaultBuildSettings = BuildSettingsVersion.V2;
        ExtraModuleNames.Add("CrossoverRebuilt");
    }
}

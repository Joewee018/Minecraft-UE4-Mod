using UnrealBuildTool;
public class CrossoverRebuiltEditorTarget : TargetRules {
    public CrossoverRebuiltEditorTarget(TargetInfo Target) : base(Target) {
        Type = TargetType.Editor;
        DefaultBuildSettings = BuildSettingsVersion.V2;
        ExtraModuleNames.Add("CrossoverRebuilt");
    }
}

using UnrealBuildTool;

public class PersistentStateTestSuite : ModuleRules
{
    public PersistentStateTestSuite(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
        OptimizeCode = CodeOptimization.InShippingBuildsOnly;
        
        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                "Core",
                "StructUtils",
                "PersistentStateGameFramework",
                "ModularGameplayActors",
                "ModularGameplay",
            }
        );

        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
                "CoreUObject",
                "Engine",
                "Slate",
                "SlateCore",
                "PersistentState",
                "CommonAutomation",
                "AutomationTest",
            }
        );
    }
}
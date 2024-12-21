using UnrealBuildTool;

public class PersistentStateGameFramework : ModuleRules
{
    public PersistentStateGameFramework(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                "Core",
                "ModularGameplayActors", 
                "ModularGameplay",
                "PersistentState",
            }
        );

        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
                "CoreUObject",
                "Engine",
            }
        );
    }
}
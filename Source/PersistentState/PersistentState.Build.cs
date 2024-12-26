// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class PersistentState : ModuleRules
{
	public PersistentState(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		OptimizeCode = CodeOptimization.InShippingBuildsOnly;
		
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core", 
				"Engine",
				"TraceLog",
				"DeveloperSettings",
				"StructUtils", 
			}
		);
			
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"Slate",
				"SlateCore",
			}
		);
		
		PublicDefinitions.AddRange(
		new string[]
		{
			"WITH_ACTOR_CUSTOM_SERIALIZE=1",
			"WITH_COMPONENT_CUSTOM_SERIALIZE=1",
			"WITH_EDITOR_COMPATIBILITY=!UE_BUILD_SHIPPING"
		});

		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.AddRange(new string[] { "UnrealEd" });
		}
	}
}

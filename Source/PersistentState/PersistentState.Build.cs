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
				"TraceLog",
				"DeveloperSettings",
				"StructUtils",
			}
		);
			
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"Engine",
				"Slate",
				"SlateCore",
			}
		);
		
		PublicDefinitions.AddRange(
		new string[]
		{
			"WITH_ACTOR_CUSTOM_SERIALIZE=0",
			"WITH_COMPONENT_CUSTOM_SERIALIZE=0",
		});
	}
}

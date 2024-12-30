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
				"XmlSerialization", 
				"JsonSerialization",
			}
		);
		
		PublicDefinitions.AddRange(
		new string[]
		{
			// Object IDs are initialized and stored with ObjectName
			"WITH_OBJECT_NAME = WITH_EDITORONLY_DATA",
			// Object references are sanitized for being able to restored
			"WITH_SANITIZE_REFERENCES  = WITH_EDITOR || WITH_EDITORONLY_DATA || UE_BUILD_DEBUG || UE_BUILD_DEVELOPMENT",
			// PIE and packaged games saves are compatible
			"WITH_EDITOR_COMPATIBILITY = WITH_EDITOR || WITH_EDITORONLY_DATA || UE_BUILD_DEBUG || UE_BUILD_DEVELOPMENT",
			// Enables custom delta serialization to optimize memory storage
			// Disabled in editor builds to allow property-based structured serialization
			"WITH_COMPACT_SERIALIZATION = !WITH_EDITOR_COMPATIBILITY",
			// enable binary serialization for all archives
			"WITH_BINARY_SERIALIZATION = !WITH_EDITOR_COMPATIBILITY",
			// Enable structured serialization that enables json and xml formatters
			// Increases memory usage, do not use in release builds
			"WITH_STRUCTURED_SERIALIZATION = WITH_EDITOR_COMPATIBILITY",
		});

		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.AddRange(new string[] { "UnrealEd" });
		}
	}
}

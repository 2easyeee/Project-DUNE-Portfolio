// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

// Global shaders of the SandDeformation plugin. Global shaders must be registered before the engine compiles its shader maps, so they live in
// their own module with loading phase PostConfigInit (the runtime module, which holds the UObject classes, loads at Default).
public class SandDeformationShaders : ModuleRules
{
	public SandDeformationShaders(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "RenderCore", "RHI" });
		PrivateDependencyModuleNames.Add("Projects");
	}
}

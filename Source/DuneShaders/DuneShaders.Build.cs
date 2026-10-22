// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

// Global shaders of Project DUNE. They live in their own module because global shaders have to be registered before the engine compiles
// its shader maps (loading phase PostConfigInit), and the game module cannot load that early: its classes load assets that need plugins.
public class DuneShaders : ModuleRules
{
	public DuneShaders(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "RenderCore", "RHI" });
	}
}

// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class SandDeformation : ModuleRules
{
	public SandDeformation(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsageMode PCH = PCHUsageMode.UseExplicitOrSharedPCHs;
		PCHUsage = PCH;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine" });
		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"RenderCore", "RHI", "ProceduralMeshComponent", "ImageWrapper", "SandDeformationShaders"
		});
	}
}

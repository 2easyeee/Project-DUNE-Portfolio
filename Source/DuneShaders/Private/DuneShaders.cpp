// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "ShaderCore.h"

class FDuneShadersModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		// /Project/DUNE/DuneSandSim.usf -> <project>/Shaders/DuneSandSim.usf
		AddShaderSourceDirectoryMapping(TEXT("/Project/DUNE"), FPaths::Combine(FPaths::ProjectDir(), TEXT("Shaders")));
	}
};

IMPLEMENT_MODULE(FDuneShadersModule, DuneShaders)

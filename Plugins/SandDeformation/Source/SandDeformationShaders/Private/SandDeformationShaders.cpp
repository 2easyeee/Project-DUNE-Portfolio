// Copyright Epic Games, Inc. All Rights Reserved.

#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "ShaderCore.h"

class FSandDeformationShadersModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		// /Plugin/SandDeformation/Private/SandDeformationSim.usf -> <plugin>/Shaders/Private/SandDeformationSim.usf
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("SandDeformation"));
		if (Plugin.IsValid())
		{
			AddShaderSourceDirectoryMapping(TEXT("/Plugin/SandDeformation"), FPaths::Combine(Plugin->GetBaseDir(), TEXT("Shaders")));
		}
	}
};

IMPLEMENT_MODULE(FSandDeformationShadersModule, SandDeformationShaders)

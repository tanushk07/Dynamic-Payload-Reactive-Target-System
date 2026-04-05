#pragma once

#include "Modules/ModuleManager.h"

class FTacticalFrameworkModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};

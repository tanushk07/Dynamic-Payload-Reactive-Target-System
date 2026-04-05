#pragma once

#include "Modules/ModuleManager.h"

class FDynamicPayloadSystemModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};

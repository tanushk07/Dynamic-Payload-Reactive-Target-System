#pragma once

#include "Modules/ModuleManager.h"
#include "Logging/LogMacros.h"

// Custom log category so plugin diagnostics don't pollute LogTemp and can be
// filtered/silenced independently in shipping projects.
DECLARE_LOG_CATEGORY_EXTERN(LogDynamicPayload, Log, All);

class FDynamicPayloadSystemModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};

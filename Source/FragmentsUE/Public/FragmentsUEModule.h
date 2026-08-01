// Copyright (c) 2026 Mohammed Azif. Licensed under the MIT License — see the LICENSE file.

#pragma once

#include "Modules/ModuleManager.h"

FRAGMENTSUE_API DECLARE_LOG_CATEGORY_EXTERN(LogFragmentsUE, Log, All);

class FFragmentsUEModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};

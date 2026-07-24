// Copyright Azif. All Rights Reserved.

#pragma once

#include "Modules/ModuleManager.h"

class FFragmentsUEEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	static void GenerateMaterials();
};

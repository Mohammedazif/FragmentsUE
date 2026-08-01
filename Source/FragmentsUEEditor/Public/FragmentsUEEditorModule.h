// Copyright (c) 2026 Mohammed Azif. Licensed under the MIT License — see the LICENSE file.

#pragma once

#include "Modules/ModuleManager.h"

class FSpawnTabArgs;
class SDockTab;

class FFragmentsUEEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	/** bForceRebuild wipes the expression graph and overwrites the package, destroying user edits. */
	static void GenerateMaterials(bool bForceRebuild = false);

private:
	void RegisterDetailCustomizations();
	void UnregisterDetailCustomizations();

	void RegisterFilterTab();
	void UnregisterFilterTab();

	TSharedRef<SDockTab> SpawnFilterTab(const FSpawnTabArgs& Args);

	TArray<FName> CustomizedClassNames;
};

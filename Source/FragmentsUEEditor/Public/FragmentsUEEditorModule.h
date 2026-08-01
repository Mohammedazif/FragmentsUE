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

	/**
	 * Create the plugin's three base materials.
	 *
	 * A material that already exists is left untouched unless bForceRebuild is set:
	 * the build empties the expression graph and saves over the package, so
	 * rebuilding unasked destroys any edit the user made to it. Only the
	 * FragmentsUE.GenerateMaterial console command forces it.
	 */
	static void GenerateMaterials(bool bForceRebuild = false);

private:
	/** Hook the IFC metadata layout into the Details panel. */
	void RegisterDetailCustomizations();
	void UnregisterDetailCustomizations();

	/** Put the IFC filter panel in Window > IFC Filter. */
	void RegisterFilterTab();
	void UnregisterFilterTab();

	TSharedRef<SDockTab> SpawnFilterTab(const FSpawnTabArgs& Args);

	/** Classes registered with the property editor, so shutdown can undo exactly those. */
	TArray<FName> CustomizedClassNames;
};

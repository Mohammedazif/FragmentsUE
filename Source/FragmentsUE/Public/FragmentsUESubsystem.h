// Copyright (c) 2026 Mohammed Azif. Licensed under the MIT License — see the LICENSE file.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/EngineSubsystem.h"
#include "FragImportResult.h"
#include "FragImportOptions.h"
#include "FragmentsUESubsystem.generated.h"

/**
 * UFragmentsUESubsystem — Runtime subsystem for loading .frag files.
 * 
 * Provides Blueprint-exposed and console-command access to the Fragments parser.
 * In Phase 1, this is primarily used for testing the parser via console commands.
 * In Phase 6, this will be the main runtime loading API.
 */
UCLASS()
class FRAGMENTSUE_API UFragmentsUESubsystem : public UEngineSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/**
	 * Load and parse a .frag file. Returns the parsed result.
	 * @param FilePath   Absolute path to the .frag file
	 * @param Options    Import options
	 * @return           Parsed result (check bSuccess)
	 */
	UFUNCTION(BlueprintCallable, Category = "FragmentsUE")
	FFragImportResult LoadFragFile(const FString& FilePath, FFragImportOptions Options);

	/**
	 * Load a .frag file and spawn it as an actor in the world.
	 */
	UFUNCTION(BlueprintCallable, Category = "FragmentsUE", meta = (WorldContext = "WorldContextObject"))
	class AFragmentsActor* SpawnFragmentsActor(
		UObject* WorldContextObject, 
		const FString& FilePath, 
		FFragImportOptions Options, 
		class UMaterialInterface* BaseMaterial);

private:
	/** Console command handler for testing: FragmentsUE.Parse <filepath> */
	void HandleParseCommand(const TArray<FString>& Args);

	/** Console command handler for testing: FragmentsUE.Import <filepath> */
	void HandleImportCommand(const TArray<FString>& Args);

	/** Registered console commands. */
	TSharedPtr<IConsoleCommand> ParseCommand;
	TSharedPtr<IConsoleCommand> ImportCommand;
};

// Copyright (c) 2026 Mohammed Azif. Licensed under the MIT License — see the LICENSE file.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/EngineSubsystem.h"
#include "FragImportResult.h"
#include "FragImportOptions.h"
#include "FragmentsUESubsystem.generated.h"

UCLASS()
class FRAGMENTSUE_API UFragmentsUESubsystem : public UEngineSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	UFUNCTION(BlueprintCallable, Category = "FragmentsUE")
	FFragImportResult LoadFragFile(const FString& FilePath, FFragImportOptions Options);

	UFUNCTION(BlueprintCallable, Category = "FragmentsUE", meta = (WorldContext = "WorldContextObject"))
	class AFragmentsActor* SpawnFragmentsActor(
		UObject* WorldContextObject, 
		const FString& FilePath, 
		FFragImportOptions Options, 
		class UMaterialInterface* BaseMaterial);

private:
	void HandleParseCommand(const TArray<FString>& Args);

	void HandleImportCommand(const TArray<FString>& Args);

	TSharedPtr<IConsoleCommand> ParseCommand;
	TSharedPtr<IConsoleCommand> ImportCommand;
};

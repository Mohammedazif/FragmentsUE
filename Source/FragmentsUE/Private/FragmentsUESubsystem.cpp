// Copyright (c) 2026 Mohammed Azif. Licensed under the MIT License — see the LICENSE file.

#include "FragmentsUESubsystem.h"
#include "FragmentsActor.h"
#include "FragParser.h"
#include "FragmentsUEModule.h"
#include "HAL/IConsoleManager.h"
#include "Misc/ScopedSlowTask.h"

void UFragmentsUESubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	ParseCommand = TSharedPtr<IConsoleCommand>(
		IConsoleManager::Get().RegisterConsoleCommand(
			TEXT("FragmentsUE.Parse"),
			TEXT("Parse a .frag file and log results. Usage: FragmentsUE.Parse <filepath>"),
			FConsoleCommandWithArgsDelegate::CreateUObject(this, &UFragmentsUESubsystem::HandleParseCommand),
			ECVF_Default
		),
		[](IConsoleCommand* Cmd)
		{
			IConsoleManager::Get().UnregisterConsoleObject(Cmd);
		}
	);

	ImportCommand = TSharedPtr<IConsoleCommand>(
		IConsoleManager::Get().RegisterConsoleCommand(
			TEXT("FragmentsUE.Import"),
			TEXT("Load a .frag file and spawn an actor in the world. Usage: FragmentsUE.Import <filepath>"),
			FConsoleCommandWithArgsDelegate::CreateUObject(this, &UFragmentsUESubsystem::HandleImportCommand),
			ECVF_Default
		),
		[](IConsoleCommand* Cmd)
		{
			IConsoleManager::Get().UnregisterConsoleObject(Cmd);
		}
	);

}

void UFragmentsUESubsystem::Deinitialize()
{
	ParseCommand.Reset();
	ImportCommand.Reset();
	Super::Deinitialize();
}

FFragImportResult UFragmentsUESubsystem::LoadFragFile(const FString& FilePath, FFragImportOptions Options)
{
#if WITH_EDITOR
	FScopedSlowTask SlowTask(1.0f, FText::FromString("Importing Fragments Model..."));
	SlowTask.MakeDialog();
	SlowTask.EnterProgressFrame(1.0f);
#endif

	return FFragParser::LoadFromFile(FilePath, Options);
}

void UFragmentsUESubsystem::HandleParseCommand(const TArray<FString>& Args)
{
	if (Args.Num() < 1)
	{
		UE_LOG(LogFragmentsUE, Warning, TEXT("Usage: FragmentsUE.Parse <filepath>"));
		return;
	}

	FString FilePath = Args[0];
	for (int32 i = 1; i < Args.Num(); i++)
	{
		FilePath += TEXT(" ") + Args[i];
	}

	FFragImportOptions Options;
	FFragImportResult Result = FFragParser::LoadFromFile(FilePath, Options);

	if (Result.bSuccess)
	{
		UE_LOG(LogFragmentsUE, Display, TEXT("%s: %d geometries, %d instances, %d elements, %d categories, %d metadata items"),
			*FPaths::GetCleanFilename(FilePath),
			Result.Geometries.Num(), Result.Instances.Num(), Result.TotalElements,
			Result.Categories.Num(), Result.Items.Num());
	}
	else
	{
		UE_LOG(LogFragmentsUE, Error, TEXT("Parse FAILED: %s"), *Result.ErrorMessage);
	}
}

AFragmentsActor* UFragmentsUESubsystem::SpawnFragmentsActor(
	UObject* WorldContextObject, 
	const FString& FilePath, 
	FFragImportOptions Options, 
	UMaterialInterface* BaseMaterial)
{
	if (!WorldContextObject || !WorldContextObject->GetWorld())
	{
		UE_LOG(LogFragmentsUE, Error, TEXT("SpawnFragmentsActor: Invalid world context"));
		return nullptr;
	}

	FFragImportResult Result = FFragParser::LoadFromFile(FilePath, Options);
	if (!Result.bSuccess)
	{
		UE_LOG(LogFragmentsUE, Error, TEXT("SpawnFragmentsActor: Parse failed - %s"), *Result.ErrorMessage);
		return nullptr;
	}

	if (!BaseMaterial)
	{
		BaseMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/FragmentsUE/M_FragBase.M_FragBase"));
	}
	UMaterialInterface* TranslucentMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/FragmentsUE/M_FragBase_Translucent.M_FragBase_Translucent"));
	if (!TranslucentMaterial)
	{
		UE_LOG(LogFragmentsUE, Error, TEXT("SpawnFragmentsActor: Failed to load /FragmentsUE/M_FragBase_Translucent.M_FragBase_Translucent!"));
	}

	UMaterialInterface* GlassMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/FragmentsUE/M_FragBase_Glass.M_FragBase_Glass"));
	if (!GlassMaterial)
	{
		UE_LOG(LogFragmentsUE, Error, TEXT("SpawnFragmentsActor: Failed to load /FragmentsUE/M_FragBase_Glass.M_FragBase_Glass!"));
	}
	
	if (!BaseMaterial)
	{
		BaseMaterial = UMaterial::GetDefaultMaterial(MD_Surface);
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	
	AFragmentsActor* ModelActor = WorldContextObject->GetWorld()->SpawnActor<AFragmentsActor>(FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
	if (ModelActor)
	{
		ModelActor->BuildFromImportResult(Result, Options, BaseMaterial, TranslucentMaterial, GlassMaterial);
	}

	return ModelActor;
}

void UFragmentsUESubsystem::HandleImportCommand(const TArray<FString>& Args)
{
	if (Args.Num() < 1)
	{
		UE_LOG(LogFragmentsUE, Warning, TEXT("Usage: FragmentsUE.Import <filepath>"));
		return;
	}

	FString FilePath = Args[0];
	bool bHierarchy = false;

	for (int32 i = 1; i < Args.Num(); i++)
	{
		if (Args[i] == TEXT("-hierarchy"))
		{
			bHierarchy = true;
		}
		else
		{
			FilePath += TEXT(" ") + Args[i];
		}
	}

	// A console command carries no world context, so one has to be found on GEngine.
	UWorld* World = nullptr;
	if (GEngine)
	{
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (Context.WorldType == EWorldType::PIE || Context.WorldType == EWorldType::Game)
			{
				World = Context.World();
				break;
			}
		}

		if (!World)
		{
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (Context.WorldType == EWorldType::Editor)
				{
					World = Context.World();
					break;
				}
			}
		}
	}

	if (!World)
	{
		UE_LOG(LogFragmentsUE, Error, TEXT("FragmentsUE.Import: Could not find an active World to spawn into."));
		return;
	}

	FFragImportOptions Options;
	Options.ScaleFactor = 100.0f;
	Options.ImportMode = bHierarchy ? EFragImportMode::HierarchyPerBody : EFragImportMode::Instanced;

	UMaterialInterface* BaseMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/FragmentsUE/M_FragBase.M_FragBase"));
	if (!BaseMaterial)
	{
		UE_LOG(LogFragmentsUE, Warning, TEXT("Could not find /FragmentsUE/M_FragBase! Falling back to BasicShapeMaterial. Materials will not have colors until you create M_FragBase in the plugin's Content folder."));
		BaseMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	}

	SpawnFragmentsActor(World, FilePath, Options, BaseMaterial);
}

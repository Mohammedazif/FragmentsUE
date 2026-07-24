// Copyright Azif. All Rights Reserved.

#include "FragmentsUESubsystem.h"
#include "FragmentsActor.h"
#include "FragParser.h"
#include "FragmentsUEModule.h"
#include "HAL/IConsoleManager.h"
#include "Misc/ScopedSlowTask.h"

void UFragmentsUESubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// Register console command for testing: FragmentsUE.Parse <filepath>
	ParseCommand = TSharedPtr<IConsoleCommand>(
		IConsoleManager::Get().RegisterConsoleCommand(
			TEXT("FragmentsUE.Parse"),
			TEXT("Parse a .frag file and log results. Usage: FragmentsUE.Parse <filepath>"),
			FConsoleCommandWithArgsDelegate::CreateUObject(this, &UFragmentsUESubsystem::HandleParseCommand),
			ECVF_Default
		),
		[](IConsoleCommand* Cmd)
		{
			// Custom deleter: unregister on destruction
			IConsoleManager::Get().UnregisterConsoleObject(Cmd);
		}
	);

	// Register console command for testing: FragmentsUE.Import <filepath>
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

	UE_LOG(LogFragmentsUE, Log, TEXT("FragmentsUE subsystem initialized. Use 'FragmentsUE.Import <path>' to test."));
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
		UE_LOG(LogFragmentsUE, Warning, TEXT("Example: FragmentsUE.Parse D:/Projects/FragImporter/models/AR520.frag"));
		return;
	}

	FString FilePath = Args[0];
	// Handle paths with spaces by joining remaining args
	for (int32 i = 1; i < Args.Num(); i++)
	{
		FilePath += TEXT(" ") + Args[i];
	}

	UE_LOG(LogFragmentsUE, Log, TEXT("═══════════════════════════════════════════════════"));
	UE_LOG(LogFragmentsUE, Log, TEXT("  FragmentsUE.Parse: %s"), *FilePath);
	UE_LOG(LogFragmentsUE, Log, TEXT("═══════════════════════════════════════════════════"));

	FFragImportOptions Options;
	FFragImportResult Result = FFragParser::LoadFromFile(FilePath, Options);

	if (Result.bSuccess)
	{
		UE_LOG(LogFragmentsUE, Log, TEXT("Parse SUCCEEDED"));

		// Log some instance details for debugging
		int32 LogCount = FMath::Min(Result.Instances.Num(), 10);
		for (int32 i = 0; i < LogCount; i++)
		{
			const auto& Inst = Result.Instances[i];
			UE_LOG(LogFragmentsUE, Log,
				TEXT("  Instance[%d]: LocalId=%d, GUID=%s, GeomIdx=%d, MatIdx=%d, Pos=(%s)"),
				i, Inst.LocalId, *Inst.GUID, Inst.GeometryIndex, Inst.MaterialIndex,
				*Inst.Transform.GetTranslation().ToString());
		}
		if (Result.Instances.Num() > 10)
		{
			UE_LOG(LogFragmentsUE, Log, TEXT("  ... (%d more instances)"),
				Result.Instances.Num() - 10);
		}

		// Log geometry details
		int32 GeomLogCount = FMath::Min(Result.Geometries.Num(), 10);
		for (int32 i = 0; i < GeomLogCount; i++)
		{
			const auto& Geom = Result.Geometries[i];
			UE_LOG(LogFragmentsUE, Log,
				TEXT("  Geometry[%d]: %d verts, %d tris"),
				i, Geom.Positions.Num(), Geom.Indices.Num() / 3);
		}
		if (Result.Geometries.Num() > 10)
		{
			UE_LOG(LogFragmentsUE, Log, TEXT("  ... (%d more geometries)"),
				Result.Geometries.Num() - 10);
		}

		// Log categories
		for (const FString& Cat : Result.Categories)
		{
			UE_LOG(LogFragmentsUE, Log, TEXT("  Category: %s"), *Cat);
		}

		// Log spatial structure (top 2 levels)
		if (Result.SpatialRoot.Children.Num() > 0)
		{
			UE_LOG(LogFragmentsUE, Log, TEXT("  Spatial Structure:"));
			for (const auto& Child : Result.SpatialRoot.Children)
			{
				UE_LOG(LogFragmentsUE, Log, TEXT("    [%d] %s (%d children)"),
					Child.LocalId, *Child.Category, Child.Children.Num());
				for (const auto& GrandChild : Child.Children)
				{
					UE_LOG(LogFragmentsUE, Log, TEXT("      [%d] %s (%d children)"),
						GrandChild.LocalId, *GrandChild.Category, GrandChild.Children.Num());
				}
			}
		}
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

	// We need a world context. Since it's a console command, we can try to find a world from GEngine.
	UWorld* World = nullptr;
	if (GEngine)
	{
		// Try to find the first PIE or Game world first
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (Context.WorldType == EWorldType::PIE || Context.WorldType == EWorldType::Game)
			{
				World = Context.World();
				break;
			}
		}
		
		// If no PIE world, try to find an Editor world
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
	Options.MeshMode = EFragMeshMode::Static; // Default to Static Mesh for now
	Options.ScaleFactor = 100.0f; // Default scale
	Options.bImportAsHierarchy = bHierarchy;

	// Load the default base material from the plugin's content folder
	UMaterialInterface* BaseMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/FragmentsUE/M_FragBase.M_FragBase"));
	if (!BaseMaterial)
	{
		UE_LOG(LogFragmentsUE, Warning, TEXT("Could not find /FragmentsUE/M_FragBase! Falling back to BasicShapeMaterial. Materials will not have colors until you create M_FragBase in the plugin's Content folder."));
		BaseMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	}

	AFragmentsActor* Actor = SpawnFragmentsActor(World, FilePath, Options, BaseMaterial);
	if (Actor)
	{
		UE_LOG(LogFragmentsUE, Log, TEXT("FragmentsUE.Import: Spawned actor %s successfully."), *Actor->GetName());
	}
}

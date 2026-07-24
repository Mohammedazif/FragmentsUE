// Copyright Azif. All Rights Reserved.

#include "ActorFactoryFragmentsModel.h"
#include "FragmentsModelAsset.h"
#include "FragmentsActor.h"
#include "FragmentsUESubsystem.h"
#include "Containers/Ticker.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Misc/Paths.h"

UActorFactoryFragmentsModel::UActorFactoryFragmentsModel()
{
	DisplayName = FText::FromString("Fragments Model");
	NewActorClass = AFragmentsActor::StaticClass();
}

bool UActorFactoryFragmentsModel::CanCreateActorFrom(const FAssetData& AssetData, FText& OutErrorMsg)
{
	if (AssetData.IsValid() && AssetData.GetClass()->IsChildOf(UFragmentsModelAsset::StaticClass()))
	{
		return true;
	}
	return false;
}

void UActorFactoryFragmentsModel::PostSpawnActor(UObject* Asset, AActor* NewActor)
{
	Super::PostSpawnActor(Asset, NewActor);

	UFragmentsModelAsset* FragAsset = Cast<UFragmentsModelAsset>(Asset);
	AFragmentsActor* FragActor = Cast<AFragmentsActor>(NewActor);

	if (FragAsset && FragActor && FragActor->GetWorld())
	{
		// Skip doing heavy import work for the transparent preview actor that follows the mouse during drag-and-drop
		if (NewActor->HasAnyFlags(RF_Transient) || NewActor->bIsEditorPreviewActor)
		{
			return;
		}

		UFragmentsUESubsystem* Subsystem = GEngine->GetEngineSubsystem<UFragmentsUESubsystem>();
		if (Subsystem)
		{
			FFragImportOptions Options;
			Options.MeshMode = EFragMeshMode::Static;
			Options.ScaleFactor = 100.0f;
			Options.bImportAsHierarchy = true;

			// Get materials
			UMaterialInterface* BaseMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/FragmentsUE/M_FragBase.M_FragBase"));
			UMaterialInterface* TranslucentMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/FragmentsUE/M_FragBase_Translucent.M_FragBase_Translucent"));
			UMaterialInterface* GlassMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/FragmentsUE/M_FragBase_Glass.M_FragBase_Glass"));

			if (!BaseMaterial) BaseMaterial = UMaterial::GetDefaultMaterial(MD_Surface);

			// The parsing is now handled with its own progress bar inside LoadFragFile.
			FFragImportResult Result = Subsystem->LoadFragFile(FragAsset->SourceFilePath, Options);
			if (Result.bSuccess)
			{
				TSharedPtr<FFragImportResult> SharedResult = MakeShared<FFragImportResult>(MoveTemp(Result));
				FString FilePath = FragAsset->SourceFilePath;

				// Delay the actual spawning by 1 second.
				FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([FragActor, SharedResult, Options, BaseMaterial, TranslucentMaterial, GlassMaterial, FilePath](float DeltaTime)
				{
					// Ensure the actor is valid, in the active world, not being destroyed, and IS NOT a preview/transient actor
					if (IsValid(FragActor) && SharedResult.IsValid() && FragActor->GetWorld() != nullptr && 
						!FragActor->IsActorBeingDestroyed() && !FragActor->HasAnyFlags(RF_Transient) && !FragActor->bIsEditorPreviewActor)
					{
						FragActor->BuildFromImportResult(*SharedResult, Options, BaseMaterial, TranslucentMaterial, GlassMaterial);
						
						// Show the custom notification exactly once for the final actor
						FNotificationInfo Info(FText::FromString(FString::Printf(TEXT("Imported: %s"), *FPaths::GetCleanFilename(FilePath))));
						Info.ExpireDuration = 3.0f;
						Info.bFireAndForget = true;
						Info.bUseSuccessFailIcons = false; // Ensures no checkmark icon is shown
						FSlateNotificationManager::Get().AddNotification(Info);
					}
					return false; // Run only once
				}), 1.0f);
			}
		}
	}
}

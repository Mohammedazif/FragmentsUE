// Copyright (c) 2026 Mohammed Azif. Licensed under the MIT License — see the LICENSE file.

#include "ActorFactoryFragmentsModel.h"
#include "FragmentsModelAsset.h"
#include "FragmentsActor.h"
#include "FragmentsUESubsystem.h"
#include "FragmentsUEModule.h"
#include "Containers/Ticker.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Misc/Paths.h"
#include "UObject/StrongObjectPtr.h"

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

FString UActorFactoryFragmentsModel::GetDefaultActorLabel(UObject* Asset) const
{
	if (UFragmentsModelAsset* FragAsset = Cast<UFragmentsModelAsset>(Asset))
	{
		return FragAsset->GetName();
	}
	return Super::GetDefaultActorLabel(Asset);
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
			FFragImportOptions Options = FragAsset->ImportOptions;

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

				// TFunction captures are invisible to the GC, so the actor is held weakly:
				// opening a level inside the delay window destroys it. The materials are rooted
				// for as long as the delegate is pending so it cannot dereference a stale
				// pointer if the /FragmentsUE package is unloaded.
				TWeakObjectPtr<AFragmentsActor> WeakActor(FragActor);
				TStrongObjectPtr<UMaterialInterface> BaseMaterialRef(BaseMaterial);
				TStrongObjectPtr<UMaterialInterface> TranslucentMaterialRef(TranslucentMaterial);
				TStrongObjectPtr<UMaterialInterface> GlassMaterialRef(GlassMaterial);

				// Delay the actual spawning by 1 second.
				FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakActor, SharedResult, Options, BaseMaterialRef, TranslucentMaterialRef, GlassMaterialRef, FilePath](float DeltaTime)
				{
					AFragmentsActor* TargetActor = WeakActor.Get();

					// Ensure the actor is valid, in the active world, not being destroyed, and IS NOT a preview/transient actor
					if (IsValid(TargetActor) && SharedResult.IsValid() && TargetActor->GetWorld() != nullptr && 
						!TargetActor->IsActorBeingDestroyed() && !TargetActor->HasAnyFlags(RF_Transient) && !TargetActor->bIsEditorPreviewActor)
					{
						TargetActor->BuildFromImportResult(*SharedResult, Options, BaseMaterialRef.Get(), TranslucentMaterialRef.Get(), GlassMaterialRef.Get());
						
						// Show the custom notification exactly once for the final actor.
						// Each Printf takes its format as a literal: UE 5.6 checks it at
						// compile time, so a ternary in that position will not bind.
						const bool bCancelled = TargetActor->bImportWasCancelled;
						const FString FileName = FPaths::GetCleanFilename(FilePath);
						const FString Message = bCancelled
							? FString::Printf(TEXT("Import cancelled: %s"), *FileName)
							: FString::Printf(TEXT("Imported: %s"), *FileName);

						FNotificationInfo Info(FText::FromString(Message));
						Info.ExpireDuration = bCancelled ? 5.0f : 3.0f;
						Info.bFireAndForget = true;
						Info.bUseSuccessFailIcons = false; // Ensures no checkmark icon is shown
						FSlateNotificationManager::Get().AddNotification(Info);
					}
					return false; // Run only once
				}), 1.0f);
			}
			else
			{
				// Without this a rejected file produces no UI at all — the actor is simply
				// empty, which reads as the plugin doing nothing rather than as a failure.
				UE_LOG(LogFragmentsUE, Error, TEXT("Import failed for %s: %s"), *FragAsset->SourceFilePath, *Result.ErrorMessage);

				FNotificationInfo Info(FText::FromString(FString::Printf(
					TEXT("Could not import %s — %s"),
					*FPaths::GetCleanFilename(FragAsset->SourceFilePath),
					*Result.ErrorMessage)));
				Info.ExpireDuration = 8.0f;
				Info.bFireAndForget = true;
				FSlateNotificationManager::Get().AddNotification(Info);
			}
		}
	}
}

// Copyright (c) 2026 Mohammed Azif. Licensed under the MIT License — see the LICENSE file.

#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "FragImportOptions.generated.h"

UENUM(BlueprintType)
enum class EFragImportMode : uint8
{
	HierarchyPerBody    UMETA(DisplayName = "Hierarchy - Actor per Body",
		ToolTip = "Full spatial tree, one actor per body. Every body selectable. Smallest on disk, most actors."),

	HierarchyPerElement UMETA(DisplayName = "Hierarchy - Actor per Element",
		ToolTip = "Full spatial tree, one actor per element. Per-element selection. Fewer actors, larger on disk."),

	HierarchyPerStorey  UMETA(DisplayName = "Hierarchy - Merged per Storey",
		ToolTip = "Full spatial tree, one actor per storey. Per-level visibility, no element selection."),

	Instanced           UMETA(DisplayName = "Instanced (no hierarchy)",
		ToolTip = "One actor of GPU-instanced meshes. Fast to render, metadata by trace only."),

	Procedural          UMETA(DisplayName = "Procedural Mesh (no hierarchy)",
		ToolTip = "One actor of procedural meshes, rebuilt on load. For runtime iteration."),

	MergedWholeModel    UMETA(DisplayName = "Merged Whole Model",
		ToolTip = "Whole model welded per material. Fewest draw calls, no element identity.")
};

FORCEINLINE bool FragModeUsesHierarchy(EFragImportMode Mode)
{
	return Mode == EFragImportMode::HierarchyPerBody
		|| Mode == EFragImportMode::HierarchyPerElement
		|| Mode == EFragImportMode::HierarchyPerStorey;
}

FORCEINLINE bool FragModeIsMerged(EFragImportMode Mode)
{
	return Mode == EFragImportMode::HierarchyPerElement
		|| Mode == EFragImportMode::HierarchyPerStorey
		|| Mode == EFragImportMode::MergedWholeModel;
}

USTRUCT(BlueprintType)
struct FRAGMENTSUE_API FFragImportOptions
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FragmentsUE")
	EFragImportMode ImportMode = EFragImportMode::HierarchyPerBody;

	/** Scale applied to all positions. Fragments is in meters, Unreal in centimeters. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FragmentsUE", meta = (ClampMin = "0.01"))
	float ScaleFactor = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FragmentsUE|Metadata")
	bool bImportMetadata = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FragmentsUE|Metadata", meta = (EditCondition = "bImportMetadata"))
	bool bImportPropertySets = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FragmentsUE|Metadata", meta = (EditCondition = "bImportMetadata"))
	bool bAttachMetadataComponents = true;

	/** Save meshes and materials to the Content Browser. Required to save or package the level. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FragmentsUE|Assets")
	bool bSaveAsAssets = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FragmentsUE|Assets", meta = (EditCondition = "bSaveAsAssets"))
	FString AssetPath = TEXT("/Game/Fragments");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FragmentsUE|Picking")
	bool bEnableElementPicking = true;

	/** Hidden because Visibility has covered every case so far; add EditAnywhere to surface it. */
	UPROPERTY()
	TEnumAsByte<ECollisionChannel> PickingTraceChannel = ECC_Visibility;
};

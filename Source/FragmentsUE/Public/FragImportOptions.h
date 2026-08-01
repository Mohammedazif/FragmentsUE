// Copyright (c) 2026 Mohammed Azif. Licensed under the MIT License — see the LICENSE file.

#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "FragImportOptions.generated.h"

/** How the model is turned into actors and meshes. */
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

/** True for the modes that build the spatial actor tree. */
FORCEINLINE bool FragModeUsesHierarchy(EFragImportMode Mode)
{
	return Mode == EFragImportMode::HierarchyPerBody
		|| Mode == EFragImportMode::HierarchyPerElement
		|| Mode == EFragImportMode::HierarchyPerStorey;
}

/** True for the modes that weld elements together, losing per-element components. */
FORCEINLINE bool FragModeIsMerged(EFragImportMode Mode)
{
	return Mode == EFragImportMode::HierarchyPerElement
		|| Mode == EFragImportMode::HierarchyPerStorey
		|| Mode == EFragImportMode::MergedWholeModel;
}

/**
 * Import options for .frag files. Carried through the entire pipeline.
 */
USTRUCT(BlueprintType)
struct FRAGMENTSUE_API FFragImportOptions
{
	GENERATED_BODY()

	/** How the model is turned into actors and meshes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FragmentsUE")
	EFragImportMode ImportMode = EFragImportMode::HierarchyPerBody;

	/** Scale applied to all positions. Fragments is in meters, Unreal in centimeters. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FragmentsUE", meta = (ClampMin = "0.01"))
	float ScaleFactor = 100.0f;

	/** Import IFC attributes, relations and GUIDs. Disable for geometry-only imports. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FragmentsUE|Metadata")
	bool bImportMetadata = true;

	/** Resolve IFC property sets and quantities onto each element. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FragmentsUE|Metadata", meta = (EditCondition = "bImportMetadata"))
	bool bImportPropertySets = true;

	/** Show the imported IFC data in the Details panel of each spawned actor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FragmentsUE|Metadata", meta = (EditCondition = "bImportMetadata"))
	bool bAttachMetadataComponents = true;

	/** Save meshes and materials to the Content Browser. Required to save or package the level. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FragmentsUE|Assets")
	bool bSaveAsAssets = true;

	/** Content path for generated assets. The model name is appended to it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FragmentsUE|Assets", meta = (EditCondition = "bSaveAsAssets"))
	FString AssetPath = TEXT("/Game/Fragments");

	/** Let traces identify elements. Nothing ever blocks movement, on or off. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FragmentsUE|Picking")
	bool bEnableElementPicking = true;

	/**
	 * Channel a picking trace must use to hit this model. Everything else is ignored,
	 * movement channels regardless.
	 *
	 * Hidden because Visibility has covered every case so far. Add EditAnywhere to
	 * surface it; SetElementPickingEnabled can also override it at runtime.
	 */
	UPROPERTY()
	TEnumAsByte<ECollisionChannel> PickingTraceChannel = ECC_Visibility;
};

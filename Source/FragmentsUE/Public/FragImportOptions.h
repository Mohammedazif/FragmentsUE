// Copyright Azif. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "FragImportOptions.generated.h"

/**
 * Mesh generation mode for imported .frag geometry.
 */
UENUM(BlueprintType)
enum class EFragMeshMode : uint8
{
	/** Baked UStaticMesh, cached to disk, Nanite-eligible. Best for editor workflows. */
	Static   UMETA(DisplayName = "Static Mesh"),

	/** Rebuilt every load via UProceduralMeshComponent. Best for runtime/iteration. */
	Dynamic  UMETA(DisplayName = "Dynamic Mesh")
};

/**
 * Import options for .frag files. Carried through the entire pipeline.
 */
USTRUCT(BlueprintType)
struct FRAGMENTSUE_API FFragImportOptions
{
	GENERATED_BODY()

	/** Mesh generation mode: Static (cached) or Dynamic (procedural). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FragmentsUE")
	EFragMeshMode MeshMode = EFragMeshMode::Static;

	/**
	 * Scale factor applied to all positions.
	 * Fragments uses meters, UE uses centimeters → default 100.0.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FragmentsUE", meta = (ClampMin = "0.01"))
	float ScaleFactor = 100.0f;

	/** Generate collision geometry for static meshes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FragmentsUE")
	bool bGenerateCollision = false;

	/** Merge all elements of the same IFC category into a single mesh. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FragmentsUE")
	bool bMergeByCategory = false;

	/** If true, imports as a hierarchy of Actors instead of Instanced Static Meshes. Warning: Impacts performance. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FragmentsUE")
	bool bImportAsHierarchy = false;
};

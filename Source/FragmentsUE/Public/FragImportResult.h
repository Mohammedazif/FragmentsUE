// Copyright Azif. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "FragImportResult.generated.h"

/**
 * Parsed geometry from one Shell (B-rep) in the .frag file.
 * Positions and normals are already transformed to UE coordinate space (LH Z-up, cm).
 */
struct FFragGeometry
{
	/** Index of this geometry in the Representations array. */
	int32 GeometryIndex = -1;

	/** Vertex positions in UE space (centimeters, LH Z-up). */
	TArray<FVector> Positions;

	/** Per-vertex normals. Computed from face winding if not present in source data. */
	TArray<FVector> Normals;

	/** Triangle indices (always int32, widened from uint16 if Shell is not "Big"). */
	TArray<int32> Indices;

	/** Axis-aligned bounding box in UE space. */
	FBox BoundingBox = FBox(ForceInit);
};

/**
 * One instance of a geometry — a transform + BIM identity.
 */
struct FFragInstance
{
	/** File-local ID for this element. */
	int32 LocalId = -1;

	/** Index into FFragImportResult::Geometries. */
	int32 GeometryIndex = -1;

	/** IFC GlobalId (GUID) string for this element. */
	FString GUID;

	/** IFC category (e.g., "IfcWall", "IfcSlab"). */
	FString Category;

	/** Human readable name of the element. */
	FString Name;

	/** Material index from the Fragments file. */
	int32 MaterialIndex = -1;

	/** World transform in UE space. */
	FTransform Transform = FTransform::Identity;

	/** Base color from the Fragments Material. */
	FLinearColor Color = FLinearColor::White;

	/** Opacity from the Fragments Material alpha channel. */
	float Opacity = 1.0f;

	/** True if RenderedFaces == TWO. */
	bool bDoubleSided = false;
};

/** 
 * Recursive spatial hierarchy node (mirrors Fragments SpatialStructure table).
 */
struct FFragSpatialNode
{
	int32 LocalId = -1;
	uint32 ExpressId = 0;
	FString Category;
	FString Name;
	TArray<FFragSpatialNode> Children;
};

/**
 * Complete parsed result from one .frag file.
 * This is the engine-agnostic intermediate representation — the decoupling layer
 * between the FlatBuffers parser and the UE mesh builder.
 */
USTRUCT(BlueprintType)
struct FRAGMENTSUE_API FFragImportResult
{
	GENERATED_BODY()

	/** Unique model GUID from the .frag file. */
	UPROPERTY(BlueprintReadOnly, Category = "FragmentsUE")
	FString ModelGuid;

	/** Human-readable model name derived from the source file path. */
	UPROPERTY(BlueprintReadOnly, Category = "FragmentsUE")
	FString ModelName;

	/** Raw JSON metadata string from the .frag file. */
	UPROPERTY(BlueprintReadOnly, Category = "FragmentsUE")
	FString Metadata;

	/** All parsed geometries (Shells). One per unique representation. */
	TArray<FFragGeometry> Geometries;

	/** All instances (Samples). Each references a geometry + transform. */
	TArray<FFragInstance> Instances;

	/** BIM spatial hierarchy tree. */
	FFragSpatialNode SpatialRoot;

	/** All unique categories found in the file. */
	UPROPERTY(BlueprintReadOnly, Category = "FragmentsUE")
	TArray<FString> Categories;

	// --- Validation stats ---
	UPROPERTY(BlueprintReadOnly, Category = "FragmentsUE")
	int32 TotalVertices = 0;

	UPROPERTY(BlueprintReadOnly, Category = "FragmentsUE")
	int32 TotalTriangles = 0;

	UPROPERTY(BlueprintReadOnly, Category = "FragmentsUE")
	int32 TotalInstances = 0;

	UPROPERTY(BlueprintReadOnly, Category = "FragmentsUE")
	int32 TotalElements = 0; // Unique GUIDs

	// --- Result ---
	UPROPERTY(BlueprintReadOnly, Category = "FragmentsUE")
	bool bSuccess = false;

	UPROPERTY(BlueprintReadOnly, Category = "FragmentsUE")
	FString ErrorMessage;
};

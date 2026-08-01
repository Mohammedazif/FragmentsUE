// Copyright (c) 2026 Mohammed Azif. Licensed under the MIT License — see the LICENSE file.

#pragma once

#include "CoreMinimal.h"
#include "FragMetadata.h"
#include "FragImportResult.generated.h"

/** Positions and normals are in UE space (LH Z-up, centimeters). */
struct FFragGeometry
{
	int32 GeometryIndex = -1;

	TArray<FVector> Positions;

	/** Computed from face winding when the source Shell has none. */
	TArray<FVector> Normals;

	/** Always int32, widened from uint16 when the Shell is not "Big". */
	TArray<int32> Indices;

	FBox BoundingBox = FBox(ForceInit);
};

struct FFragInstance
{
	int32 LocalId = -1;

	int32 GeometryIndex = -1;

	FString GUID;

	FString Category;

	FString Name;

	int32 MaterialIndex = -1;

	FTransform Transform = FTransform::Identity;

	FLinearColor Color = FLinearColor::White;

	float Opacity = 1.0f;

	/** True when the Fragments RenderedFaces value is TWO. */
	bool bDoubleSided = false;
};

struct FFragSpatialNode
{
	int32 LocalId = -1;
	uint32 ExpressId = 0;
	FString Category;
	FString Name;
	TArray<FFragSpatialNode> Children;
};

USTRUCT(BlueprintType)
struct FRAGMENTSUE_API FFragImportResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "FragmentsUE")
	FString ModelGuid;

	UPROPERTY(BlueprintReadOnly, Category = "FragmentsUE")
	FString ModelName;

	UPROPERTY(BlueprintReadOnly, Category = "FragmentsUE")
	FString Metadata;

	TArray<FFragGeometry> Geometries;

	TArray<FFragInstance> Instances;

	FFragSpatialNode SpatialRoot;

	UPROPERTY(BlueprintReadOnly, Category = "FragmentsUE")
	TArray<FString> Categories;

	/** Indexed by LocalId, parallel to the model's local_ids; empty when metadata import is off. */
	UPROPERTY(BlueprintReadOnly, Category = "FragmentsUE")
	TArray<FFragItemMetadata> Items;

	UPROPERTY(BlueprintReadOnly, Category = "FragmentsUE")
	FFragItemMetadata ModelInfo;

	const FFragItemMetadata* FindItem(int32 LocalId) const
	{
		return Items.IsValidIndex(LocalId) ? &Items[LocalId] : nullptr;
	}

	UPROPERTY(BlueprintReadOnly, Category = "FragmentsUE")
	int32 TotalVertices = 0;

	UPROPERTY(BlueprintReadOnly, Category = "FragmentsUE")
	int32 TotalTriangles = 0;

	UPROPERTY(BlueprintReadOnly, Category = "FragmentsUE")
	int32 TotalInstances = 0;

	UPROPERTY(BlueprintReadOnly, Category = "FragmentsUE")
	int32 TotalElements = 0;

	UPROPERTY(BlueprintReadOnly, Category = "FragmentsUE")
	bool bSuccess = false;

	UPROPERTY(BlueprintReadOnly, Category = "FragmentsUE")
	FString ErrorMessage;
};

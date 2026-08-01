// Copyright (c) 2026 Mohammed Azif. Licensed under the MIT License — see the LICENSE file.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Engine/StaticMeshActor.h"
#include "FragMetadata.h"
#include "FragmentsElementActor.generated.h"

class UFragmentsMetadataComponent;

/**
 * A geometry leaf of an imported model: one IFC element, one static mesh.
 *
 * Behaves exactly like AStaticMeshActor, but owns an IFC metadata component so
 * selecting it in the editor shows the element's attributes and property sets
 * in the Details panel.
 */
UCLASS(NotPlaceable, DisplayName = "Fragments Element")
class FRAGMENTSUE_API AFragmentsElementActor : public AStaticMeshActor
{
	GENERATED_BODY()

public:
	AFragmentsElementActor(const FObjectInitializer& ObjectInitializer);

	/** IFC attributes and property sets for this element. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IFC Metadata")
	TObjectPtr<UFragmentsMetadataComponent> MetadataComponent;

	/** Copy a parsed item record onto this actor. */
	void SetItemMetadata(const FFragItemMetadata& InItem);
};

/**
 * A grouping node of the BIM spatial structure — project, site, building,
 * storey, space, or an IFC category bucket.
 *
 * Carries no geometry of its own, only a transform and the node's own IFC
 * metadata (a storey's Elevation and Pset_BuildingStoreyCommon, for example).
 */
UCLASS(NotPlaceable, DisplayName = "Fragments Spatial Node")
class FRAGMENTSUE_API AFragmentsNodeActor : public AActor
{
	GENERATED_BODY()

public:
	AFragmentsNodeActor(const FObjectInitializer& ObjectInitializer);

	/** IFC attributes and property sets for this spatial node. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IFC Metadata")
	TObjectPtr<UFragmentsMetadataComponent> MetadataComponent;

	/** Copy a parsed item record onto this actor. */
	void SetItemMetadata(const FFragItemMetadata& InItem);
};

// Copyright (c) 2026 Mohammed Azif. Licensed under the MIT License — see the LICENSE file.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "FragMetadata.h"
#include "FragmentsMetadataComponent.generated.h"

/**
 * Carries the IFC metadata of a single BIM element on the actor that renders it.
 *
 * The plugin attaches one of these to every element and spatial-node actor it
 * spawns, which makes the data readable two ways:
 *   - Editor: select the actor and read the "IFC Metadata" section in Details.
 *   - Runtime: GetComponentByClass, or AFragmentsActor::GetMetadataFromHit.
 */
UCLASS(ClassGroup = (FragmentsUE), meta = (BlueprintSpawnableComponent, DisplayName = "IFC Metadata"))
class FRAGMENTSUE_API UFragmentsMetadataComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UFragmentsMetadataComponent();

	/** All IFC data for this element, straight from the .frag file. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IFC Metadata")
	FFragItemMetadata ItemData;

	/** Full metadata record for this element. */
	UFUNCTION(BlueprintPure, Category = "IFC Metadata")
	FFragItemMetadata GetItemData() const { return ItemData; }

	/** IFC GlobalId, or empty when the element has none. */
	UFUNCTION(BlueprintPure, Category = "IFC Metadata")
	FString GetGuid() const { return ItemData.GUID; }

	/** IFC entity class, e.g. "IFCWALLSTANDARDCASE". */
	UFUNCTION(BlueprintPure, Category = "IFC Metadata")
	FString GetCategory() const { return ItemData.Category; }

	/** Value of the IFC "Name" attribute. */
	UFUNCTION(BlueprintPure, Category = "IFC Metadata")
	FString GetIfcName() const { return ItemData.Name; }

	/** File-local id, usable with AFragmentsActor lookups. */
	UFUNCTION(BlueprintPure, Category = "IFC Metadata")
	int32 GetLocalId() const { return ItemData.LocalId; }

	/** Look up a direct attribute such as "ObjectType" or "Tag". */
	UFUNCTION(BlueprintPure, Category = "IFC Metadata")
	bool GetAttribute(const FString& AttributeName, FString& OutValue) const;

	/**
	 * Look up a property inside a property set.
	 * @param PropertySetName   Set to search, or leave empty to search every set.
	 */
	UFUNCTION(BlueprintPure, Category = "IFC Metadata")
	bool GetProperty(const FString& PropertySetName, const FString& PropertyName, FString& OutValue) const;

	/** Names of every property set attached to this element. */
	UFUNCTION(BlueprintPure, Category = "IFC Metadata")
	TArray<FString> GetPropertySetNames() const;

	/** Every property in one set as name → value. */
	UFUNCTION(BlueprintPure, Category = "IFC Metadata")
	TMap<FString, FString> GetPropertiesInSet(const FString& PropertySetName) const;

	/**
	 * Everything flattened into one map, ready to feed a UI widget.
	 * Attributes are keyed by name; properties are keyed "PsetName.PropertyName".
	 */
	UFUNCTION(BlueprintPure, Category = "IFC Metadata")
	TMap<FString, FString> GetAllValuesFlat() const;

	/** Unique material names on this element, including every layer of a layer set. */
	UFUNCTION(BlueprintPure, Category = "IFC Metadata")
	TArray<FString> GetMaterialNames() const;

	/** Name of the IfcTypeObject this element is an occurrence of. */
	UFUNCTION(BlueprintPure, Category = "IFC Metadata")
	FString GetTypeName() const { return ItemData.TypeName; }

	/** Name of the storey, space or element that directly contains this one. */
	UFUNCTION(BlueprintPure, Category = "IFC Metadata")
	FString GetContainerName() const { return ItemData.ContainerName; }

	/** Building storey this element sits on, resolved through nested containers. */
	UFUNCTION(BlueprintPure, Category = "IFC Metadata")
	FString GetStoreyName() const { return ItemData.StoreyName; }

	/** Ids of items this element points at through the named relationship. */
	UFUNCTION(BlueprintPure, Category = "IFC Metadata")
	TArray<int32> GetRelatedLocalIds(const FString& RelationName) const;

	/** Human-readable dump of every attribute and property, one per line. */
	UFUNCTION(BlueprintPure, Category = "IFC Metadata")
	FString ToDisplayString() const;
};

/**
 * Blueprint helpers for reaching IFC metadata without knowing the component type.
 */
UCLASS()
class FRAGMENTSUE_API UFragmentsMetadataLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Find the metadata component on an actor, walking up to its attach parent if needed. */
	UFUNCTION(BlueprintPure, Category = "FragmentsUE|Metadata", meta = (DefaultToSelf = "Actor"))
	static UFragmentsMetadataComponent* FindMetadataComponent(AActor* Actor, bool bSearchAttachParents = true);

	/** Read an actor's IFC metadata in one call. Returns false when the actor has none. */
	UFUNCTION(BlueprintPure, Category = "FragmentsUE|Metadata", meta = (DefaultToSelf = "Actor"))
	static bool GetActorMetadata(AActor* Actor, FFragItemMetadata& OutItem);
};

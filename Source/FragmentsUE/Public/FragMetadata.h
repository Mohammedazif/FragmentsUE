// Copyright (c) 2026 Mohammed Azif. Licensed under the MIT License — see the LICENSE file.

#pragma once

#include "CoreMinimal.h"
#include "FragMetadata.generated.h"

/**
 * One IFC attribute or property value.
 *
 * In a .frag file these are serialized as JSON tuples inside Attribute::data:
 *     ["Name","Basic Wall:Generic - 200mm:150702","IFCLABEL"]
 *     ["Elevation",-2.7432000000000003,"IFCLENGTHMEASURE"]
 *
 * Values are kept as strings so any IFC type (label, measure, logical, enum,
 * compound measure) survives the round trip without loss.
 */
USTRUCT(BlueprintType)
struct FRAGMENTSUE_API FFragAttribute
{
	GENERATED_BODY()

	/** Attribute name, e.g. "Name", "ObjectType", "Tag", "Elevation". */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IFC")
	FString Name;

	/** Value as text. Empty for IFC nulls. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IFC")
	FString Value;

	/** Declared IFC type, e.g. "IFCLABEL", "IFCINTEGER", "IFCLENGTHMEASURE". May be empty. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IFC")
	FString Type;

	FFragAttribute() = default;

	FFragAttribute(FString InName, FString InValue, FString InType)
		: Name(MoveTemp(InName))
		, Value(MoveTemp(InValue))
		, Type(MoveTemp(InType))
	{}
};

/**
 * An IfcPropertySet (or IfcElementQuantity) attached to an element.
 * Resolved by following IsDefinedBy / HasPropertySets → HasProperties / Quantities.
 */
USTRUCT(BlueprintType)
struct FRAGMENTSUE_API FFragPropertySet
{
	GENERATED_BODY()

	/** Set name, e.g. "Pset_WallCommon", "BaseQuantities". */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IFC")
	FString Name;

	/** File-local id of the property set item itself. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IFC")
	int32 LocalId = -1;

	/** True when this set came from the element's type rather than the occurrence. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IFC")
	bool bFromType = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IFC")
	TArray<FFragAttribute> Properties;
};

/**
 * A material associated with an element, resolved from the HasAssociations graph.
 *
 * Covers all four shapes an IFC file uses: a bare IfcMaterial, an IfcMaterialList,
 * an IfcMaterialLayerSet, and an IfcMaterialLayerSetUsage that points at one.
 */
USTRUCT(BlueprintType)
struct FRAGMENTSUE_API FFragMaterial
{
	GENERATED_BODY()

	/** Material name, e.g. "Default Floor", "Door - Panel". */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IFC")
	FString Name;

	/** Owning layer set, e.g. "Floor:Generic - 1/2\"". Empty for non-layered materials. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IFC")
	FString LayerSetName;

	/** Layer thickness in the model's IFC length unit. Zero when this is not a layer. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IFC")
	float Thickness = 0.0f;

	/** File-local id of the IfcMaterial item. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IFC")
	int32 LocalId = -1;
};

/**
 * A raw IFC relationship, e.g. ["ContainedInStructure",225].
 * Target ids are already converted to file-local (dense) ids.
 */
USTRUCT(BlueprintType)
struct FRAGMENTSUE_API FFragRelation
{
	GENERATED_BODY()

	/** Relation name, e.g. "IsDefinedBy", "ContainsElements", "HasAssociations". */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IFC")
	FString Name;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IFC")
	TArray<int32> RelatedLocalIds;
};

/**
 * Everything the .frag file knows about a single BIM item: identity, direct
 * attributes, resolved property sets, and raw relationships.
 *
 * One of these exists per entry in the model's local_ids array, so
 * FFragImportResult::Items is indexed directly by LocalId.
 */
USTRUCT(BlueprintType)
struct FRAGMENTSUE_API FFragItemMetadata
{
	GENERATED_BODY()

	/** File-local (dense) id. Matches FFragInstance::LocalId and the index in FFragImportResult::Items. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IFC")
	int32 LocalId = -1;

	/** Original IFC STEP express id (#1234). Stored as int64 because Blueprints have no uint32. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IFC")
	int64 ExpressId = 0;

	/** IFC GlobalId (22-character base64 GUID). Empty when the item has none. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IFC")
	FString GUID;

	/** IFC entity class, e.g. "IFCWALLSTANDARDCASE". */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IFC")
	FString Category;

	/** Value of the "Name" attribute, hoisted for convenience. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IFC")
	FString Name;

	/** Direct attributes of the entity (Name, ObjectType, Tag, Elevation, ...). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IFC")
	TArray<FFragAttribute> Attributes;

	/** Property sets and quantity sets resolved from the relationship graph. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IFC")
	TArray<FFragPropertySet> PropertySets;

	/** Materials and material layers resolved from HasAssociations. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IFC")
	TArray<FFragMaterial> Materials;

	/** Classification references (Uniclass, OmniClass, ...) from HasAssociations. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IFC")
	TArray<FFragAttribute> Classifications;

	/** Name of the IfcTypeObject this element is an occurrence of. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IFC")
	FString TypeName;

	/** File-local id of the type object, or -1. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IFC")
	int32 TypeLocalId = -1;

	/** Spatial container: the storey, space or site holding this element. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IFC")
	FString ContainerName;

	/** IFC class of the spatial container, e.g. "IFCBUILDINGSTOREY". */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IFC")
	FString ContainerCategory;

	/** File-local id of the spatial container, or -1. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IFC")
	int32 ContainerLocalId = -1;

	/**
	 * Building storey this element ends up on, found by walking the containment
	 * and aggregation chain. Differs from ContainerName when the element is
	 * aggregated into another element (a door inside a curtain wall, say).
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IFC")
	FString StoreyName;

	/** File-local id of the building storey, or -1. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IFC")
	int32 StoreyLocalId = -1;

	/** Raw relationships, kept so callers can walk the BIM graph themselves. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IFC")
	TArray<FFragRelation> Relations;

	bool IsEmpty() const
	{
		return Attributes.Num() == 0
			&& PropertySets.Num() == 0
			&& Materials.Num() == 0
			&& Classifications.Num() == 0
			&& Relations.Num() == 0
			&& GUID.IsEmpty();
	}

	/** Case-insensitive attribute lookup. Returns null when absent. */
	const FFragAttribute* FindAttribute(const FString& InName) const
	{
		for (const FFragAttribute& Attribute : Attributes)
		{
			if (Attribute.Name.Equals(InName, ESearchCase::IgnoreCase))
			{
				return &Attribute;
			}
		}
		return nullptr;
	}

	/** Case-insensitive property set lookup. Returns null when absent. */
	const FFragPropertySet* FindPropertySet(const FString& InName) const
	{
		for (const FFragPropertySet& Set : PropertySets)
		{
			if (Set.Name.Equals(InName, ESearchCase::IgnoreCase))
			{
				return &Set;
			}
		}
		return nullptr;
	}

	/**
	 * Look up a property inside a set.
	 * @param InSetName   Property set to search, or empty to search every set.
	 */
	const FFragAttribute* FindProperty(const FString& InSetName, const FString& InPropertyName) const
	{
		for (const FFragPropertySet& Set : PropertySets)
		{
			if (!InSetName.IsEmpty() && !Set.Name.Equals(InSetName, ESearchCase::IgnoreCase))
			{
				continue;
			}
			for (const FFragAttribute& Property : Set.Properties)
			{
				if (Property.Name.Equals(InPropertyName, ESearchCase::IgnoreCase))
				{
					return &Property;
				}
			}
		}
		return nullptr;
	}

	/** Total number of key/value pairs across attributes and every property set. */
	int32 CountValues() const
	{
		int32 Count = Attributes.Num() + Materials.Num() + Classifications.Num();
		for (const FFragPropertySet& Set : PropertySets)
		{
			Count += Set.Properties.Num();
		}
		return Count;
	}
};

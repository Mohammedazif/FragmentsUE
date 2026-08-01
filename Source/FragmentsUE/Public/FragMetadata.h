// Copyright (c) 2026 Mohammed Azif. Licensed under the MIT License — see the LICENSE file.

#pragma once

#include "CoreMinimal.h"
#include "FragMetadata.generated.h"

/** Serialized in .frag as JSON tuples in Attribute::data; kept as strings so any IFC type survives. */
USTRUCT(BlueprintType)
struct FRAGMENTSUE_API FFragAttribute
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IFC")
	FString Name;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IFC")
	FString Value;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IFC")
	FString Type;

	FFragAttribute() = default;

	FFragAttribute(FString InName, FString InValue, FString InType)
		: Name(MoveTemp(InName))
		, Value(MoveTemp(InValue))
		, Type(MoveTemp(InType))
	{}
};

USTRUCT(BlueprintType)
struct FRAGMENTSUE_API FFragPropertySet
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IFC")
	FString Name;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IFC")
	int32 LocalId = -1;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IFC")
	bool bFromType = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IFC")
	TArray<FFragAttribute> Properties;
};

USTRUCT(BlueprintType)
struct FRAGMENTSUE_API FFragMaterial
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IFC")
	FString Name;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IFC")
	FString LayerSetName;

	/** In the model's IFC length unit. Zero when this is not a layer. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IFC")
	float Thickness = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IFC")
	int32 LocalId = -1;
};

USTRUCT(BlueprintType)
struct FRAGMENTSUE_API FFragRelation
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IFC")
	FString Name;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IFC")
	TArray<int32> RelatedLocalIds;
};

USTRUCT(BlueprintType)
struct FRAGMENTSUE_API FFragItemMetadata
{
	GENERATED_BODY()

	/** Dense id; also the index into FFragImportResult::Items. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IFC")
	int32 LocalId = -1;

	/** IFC STEP express id (#1234); int64 because Blueprints have no uint32. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IFC")
	int64 ExpressId = 0;

	/** IFC GlobalId, a 22-character base64 GUID. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IFC")
	FString GUID;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IFC")
	FString Category;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IFC")
	FString Name;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IFC")
	TArray<FFragAttribute> Attributes;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IFC")
	TArray<FFragPropertySet> PropertySets;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IFC")
	TArray<FFragMaterial> Materials;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IFC")
	TArray<FFragAttribute> Classifications;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IFC")
	FString TypeName;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IFC")
	int32 TypeLocalId = -1;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IFC")
	FString ContainerName;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IFC")
	FString ContainerCategory;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IFC")
	int32 ContainerLocalId = -1;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IFC")
	FString StoreyName;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IFC")
	int32 StoreyLocalId = -1;

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

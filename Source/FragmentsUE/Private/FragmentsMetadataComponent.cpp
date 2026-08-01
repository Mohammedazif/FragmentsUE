// Copyright (c) 2026 Mohammed Azif. Licensed under the MIT License — see the LICENSE file.

#include "FragmentsMetadataComponent.h"
#include "GameFramework/Actor.h"
#include "Misc/StringBuilder.h"

UFragmentsMetadataComponent::UFragmentsMetadataComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	bAutoActivate = true;
}

bool UFragmentsMetadataComponent::GetAttribute(const FString& AttributeName, FString& OutValue) const
{
	if (const FFragAttribute* Attribute = ItemData.FindAttribute(AttributeName))
	{
		OutValue = Attribute->Value;
		return true;
	}
	OutValue.Empty();
	return false;
}

bool UFragmentsMetadataComponent::GetProperty(const FString& PropertySetName, const FString& PropertyName, FString& OutValue) const
{
	if (const FFragAttribute* Property = ItemData.FindProperty(PropertySetName, PropertyName))
	{
		OutValue = Property->Value;
		return true;
	}
	OutValue.Empty();
	return false;
}

TArray<FString> UFragmentsMetadataComponent::GetPropertySetNames() const
{
	TArray<FString> Names;
	Names.Reserve(ItemData.PropertySets.Num());
	for (const FFragPropertySet& Set : ItemData.PropertySets)
	{
		Names.Add(Set.Name);
	}
	return Names;
}

TMap<FString, FString> UFragmentsMetadataComponent::GetPropertiesInSet(const FString& PropertySetName) const
{
	TMap<FString, FString> Values;
	if (const FFragPropertySet* Set = ItemData.FindPropertySet(PropertySetName))
	{
		Values.Reserve(Set->Properties.Num());
		for (const FFragAttribute& Property : Set->Properties)
		{
			Values.Add(Property.Name, Property.Value);
		}
	}
	return Values;
}

TMap<FString, FString> UFragmentsMetadataComponent::GetAllValuesFlat() const
{
	TMap<FString, FString> Values;
	Values.Reserve(ItemData.CountValues());

	for (const FFragAttribute& Attribute : ItemData.Attributes)
	{
		Values.Add(Attribute.Name, Attribute.Value);
	}

	for (const FFragPropertySet& Set : ItemData.PropertySets)
	{
		for (const FFragAttribute& Property : Set.Properties)
		{
			Values.Add(FString::Printf(TEXT("%s.%s"), *Set.Name, *Property.Name), Property.Value);
		}
	}

	for (const FFragAttribute& Classification : ItemData.Classifications)
	{
		Values.Add(FString::Printf(TEXT("Classification.%s"), *Classification.Name), Classification.Value);
	}

	int32 MaterialIndex = 0;
	for (const FFragMaterial& Material : ItemData.Materials)
	{
		Values.Add(FString::Printf(TEXT("Material.%d"), MaterialIndex++), Material.Name);
	}

	if (!ItemData.TypeName.IsEmpty())
	{
		Values.Add(TEXT("TypeName"), ItemData.TypeName);
	}
	if (!ItemData.ContainerName.IsEmpty())
	{
		Values.Add(TEXT("ContainedIn"), ItemData.ContainerName);
	}
	if (!ItemData.StoreyName.IsEmpty())
	{
		Values.Add(TEXT("Storey"), ItemData.StoreyName);
	}

	return Values;
}

TArray<FString> UFragmentsMetadataComponent::GetMaterialNames() const
{
	TArray<FString> Names;
	Names.Reserve(ItemData.Materials.Num());
	for (const FFragMaterial& Material : ItemData.Materials)
	{
		Names.AddUnique(Material.Name);
	}
	return Names;
}

TArray<int32> UFragmentsMetadataComponent::GetRelatedLocalIds(const FString& RelationName) const
{
	TArray<int32> Related;
	for (const FFragRelation& Relation : ItemData.Relations)
	{
		if (Relation.Name.Equals(RelationName, ESearchCase::IgnoreCase))
		{
			Related.Append(Relation.RelatedLocalIds);
		}
	}
	return Related;
}

FString UFragmentsMetadataComponent::ToDisplayString() const
{
	TStringBuilder<2048> Builder;

	Builder.Appendf(TEXT("Category: %s\n"), *ItemData.Category);
	Builder.Appendf(TEXT("Name: %s\n"), *ItemData.Name);
	if (!ItemData.GUID.IsEmpty())
	{
		Builder.Appendf(TEXT("GlobalId: %s\n"), *ItemData.GUID);
	}
	if (!ItemData.TypeName.IsEmpty())
	{
		Builder.Appendf(TEXT("Type: %s\n"), *ItemData.TypeName);
	}
	if (!ItemData.ContainerName.IsEmpty())
	{
		Builder.Appendf(TEXT("Contained In: %s (%s)\n"), *ItemData.ContainerName, *ItemData.ContainerCategory);
	}
	if (!ItemData.StoreyName.IsEmpty() && ItemData.StoreyLocalId != ItemData.ContainerLocalId)
	{
		Builder.Appendf(TEXT("Storey: %s\n"), *ItemData.StoreyName);
	}
	Builder.Appendf(TEXT("LocalId: %d (Express #%lld)\n"), ItemData.LocalId, ItemData.ExpressId);

	if (ItemData.Attributes.Num() > 0)
	{
		Builder.Append(TEXT("\nAttributes\n"));
		for (const FFragAttribute& Attribute : ItemData.Attributes)
		{
			Builder.Appendf(TEXT("  %s: %s\n"), *Attribute.Name, *Attribute.Value);
		}
	}

	for (const FFragPropertySet& Set : ItemData.PropertySets)
	{
		Builder.Appendf(TEXT("\n%s%s\n"), *Set.Name, Set.bFromType ? TEXT(" (from type)") : TEXT(""));
		for (const FFragAttribute& Property : Set.Properties)
		{
			Builder.Appendf(TEXT("  %s: %s\n"), *Property.Name, *Property.Value);
		}
	}

	if (ItemData.Materials.Num() > 0)
	{
		Builder.Append(TEXT("\nMaterials\n"));
		for (const FFragMaterial& Material : ItemData.Materials)
		{
			if (Material.Thickness > 0.0f)
			{
				Builder.Appendf(TEXT("  %s: %s (%g thick)\n"),
					*Material.LayerSetName, *Material.Name, Material.Thickness);
			}
			else
			{
				Builder.Appendf(TEXT("  %s\n"), *Material.Name);
			}
		}
	}

	if (ItemData.Classifications.Num() > 0)
	{
		Builder.Append(TEXT("\nClassification\n"));
		for (const FFragAttribute& Classification : ItemData.Classifications)
		{
			Builder.Appendf(TEXT("  %s: %s\n"), *Classification.Name, *Classification.Value);
		}
	}

	return Builder.ToString();
}

// ─────────────────────────────────────────────────────────────────────────────
// Blueprint library
// ─────────────────────────────────────────────────────────────────────────────

UFragmentsMetadataComponent* UFragmentsMetadataLibrary::FindMetadataComponent(AActor* Actor, bool bSearchAttachParents)
{
	while (IsValid(Actor))
	{
		if (UFragmentsMetadataComponent* Found = Actor->FindComponentByClass<UFragmentsMetadataComponent>())
		{
			return Found;
		}

		if (!bSearchAttachParents)
		{
			break;
		}

		Actor = Actor->GetAttachParentActor();
	}

	return nullptr;
}

bool UFragmentsMetadataLibrary::GetActorMetadata(AActor* Actor, FFragItemMetadata& OutItem)
{
	if (const UFragmentsMetadataComponent* Component = FindMetadataComponent(Actor, /*bSearchAttachParents*/ true))
	{
		OutItem = Component->ItemData;
		return true;
	}

	OutItem = FFragItemMetadata();
	return false;
}

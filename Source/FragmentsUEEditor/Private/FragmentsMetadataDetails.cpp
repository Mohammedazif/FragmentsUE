// Copyright (c) 2026 Mohammed Azif. Licensed under the MIT License — see the LICENSE file.

#include "FragmentsMetadataDetails.h"
#include "FragmentsMetadataComponent.h"
#include "FragMetadata.h"

#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "DetailWidgetRow.h"
#include "IDetailGroup.h"
#include "PropertyHandle.h"

#include "GameFramework/Actor.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/SBoxPanel.h"

#define LOCTEXT_NAMESPACE "FragmentsMetadataDetails"

namespace
{
	/** Read-only name/value pair, selectable so the value can be copied out. */
	void FillTextRow(FDetailWidgetRow& Row, const FString& Name, const FString& Value, const FString& Tooltip = FString())
	{
		const FText ValueText = FText::FromString(Value);
		const FText TooltipText = Tooltip.IsEmpty() ? ValueText : FText::FromString(Tooltip);

		Row.FilterString(FText::FromString(Name + TEXT(" ") + Value))
		.NameContent()
		[
			SNew(STextBlock)
			.Text(FText::FromString(Name))
			.ToolTipText(FText::FromString(Name))
			.Font(IDetailLayoutBuilder::GetDetailFont())
		]
		.ValueContent()
		.MinDesiredWidth(220.0f)
		.MaxDesiredWidth(600.0f)
		[
			SNew(SEditableTextBox)
			.Text(ValueText)
			.ToolTipText(TooltipText)
			.IsReadOnly(true)
			.Font(IDetailLayoutBuilder::GetDetailFont())
		];
	}

	// Caps eager widget building; 200/100 far exceeds any real IFC element and avoids editor hangs.
	constexpr int32 MaxRowsPerGroup = 200;
	constexpr int32 MaxPropertySetGroups = 100;

	void FillOverflowRow(FDetailWidgetRow& Row, int32 HiddenCount)
	{
		Row.WholeRowContent()
		[
			SNew(STextBlock)
			.Text(FText::Format(
				LOCTEXT("MoreRows", "… {0} more not shown — use Copy All to Clipboard."),
				FText::AsNumber(HiddenCount)))
			.Font(IDetailLayoutBuilder::GetDetailFontItalic())
		];
	}

	FString MakeValueTooltip(const FFragAttribute& Attribute)
	{
		if (Attribute.Type.IsEmpty())
		{
			return Attribute.Value;
		}
		return FString::Printf(TEXT("%s\n\nIFC type: %s"), *Attribute.Value, *Attribute.Type);
	}
}

TSharedRef<IDetailCustomization> FFragmentsMetadataDetails::MakeInstance()
{
	return MakeShareable(new FFragmentsMetadataDetails);
}

UFragmentsMetadataComponent* FFragmentsMetadataDetails::ResolveComponent(const TArray<TWeakObjectPtr<UObject>>& Objects)
{
	for (const TWeakObjectPtr<UObject>& WeakObject : Objects)
	{
		UObject* Object = WeakObject.Get();
		if (!Object)
		{
			continue;
		}

		if (UFragmentsMetadataComponent* Component = Cast<UFragmentsMetadataComponent>(Object))
		{
			return Component;
		}

		if (AActor* Actor = Cast<AActor>(Object))
		{
			if (UFragmentsMetadataComponent* Component = Actor->FindComponentByClass<UFragmentsMetadataComponent>())
			{
				return Component;
			}
		}
	}

	return nullptr;
}

void FFragmentsMetadataDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<TWeakObjectPtr<UObject>> Objects;
	DetailBuilder.GetObjectsBeingCustomized(Objects);

	UFragmentsMetadataComponent* Component = ResolveComponent(Objects);
	if (!Component)
	{
		return;
	}

	{
		const TSharedRef<IPropertyHandle> ItemDataHandle = DetailBuilder.GetProperty(TEXT("ItemData"));
		if (ItemDataHandle->IsValidHandle())
		{
			DetailBuilder.HideProperty(ItemDataHandle);
		}

		const TSharedRef<IPropertyHandle> ComponentHandle = DetailBuilder.GetProperty(TEXT("MetadataComponent"));
		if (ComponentHandle->IsValidHandle())
		{
			DetailBuilder.HideProperty(ComponentHandle);
		}

		const TSharedRef<IPropertyHandle> ModelComponentHandle = DetailBuilder.GetProperty(TEXT("ModelMetadataComponent"));
		if (ModelComponentHandle->IsValidHandle())
		{
			DetailBuilder.HideProperty(ModelComponentHandle);
		}
	}

	// EditCategory returns the same category for actor and component layouts, so skip the duplicate.
	const UClass* BaseClass = DetailBuilder.GetBaseClass();
	const bool bCustomizingComponent = Objects.Num() > 0
		&& Objects[0].IsValid()
		&& Objects[0]->IsA(UFragmentsMetadataComponent::StaticClass());

	if (bCustomizingComponent && BaseClass && BaseClass->IsChildOf(AActor::StaticClass()))
	{
		return;
	}

	IDetailCategoryBuilder& Category = DetailBuilder.EditCategory(
		TEXT("IFC Metadata"),
		LOCTEXT("IfcMetadataCategory", "IFC Metadata"),
		ECategoryPriority::Important);

	const FFragItemMetadata& Item = Component->ItemData;

	if (Item.IsEmpty() && Item.Name.IsEmpty() && Item.Category.IsEmpty())
	{
		Category.AddCustomRow(LOCTEXT("NoMetadataFilter", "IFC Metadata"))
		.WholeRowContent()
		[
			SNew(STextBlock)
			.Text(LOCTEXT("NoMetadata", "No IFC metadata on this element. Re-import with 'Import Metadata' enabled."))
			.Font(IDetailLayoutBuilder::GetDetailFontItalic())
		];
		return;
	}

	if (Objects.Num() > 1)
	{
		Category.AddCustomRow(LOCTEXT("MultiSelectFilter", "Selection"))
		.WholeRowContent()
		[
			SNew(STextBlock)
			.Text(FText::Format(
				LOCTEXT("MultiSelect", "{0} objects selected — showing the first."),
				FText::AsNumber(Objects.Num())))
			.Font(IDetailLayoutBuilder::GetDetailFontItalic())
		];
	}

	BuildIdentityRows(Category, Item);
	BuildAttributeRows(Category, Item);
	BuildPropertySetRows(Category, Item);
	BuildMaterialRows(Category, Item);
	BuildClassificationRows(Category, Item);
	BuildRelationRows(Category, Item);

	TWeakObjectPtr<UFragmentsMetadataComponent> WeakComponent(Component);
	Category.AddCustomRow(LOCTEXT("CopyFilter", "Copy IFC Metadata"))
	.WholeRowContent()
	[
		SNew(SButton)
		.HAlign(HAlign_Center)
		.ToolTipText(LOCTEXT("CopyTooltip", "Copy every attribute and property of this element to the clipboard."))
		.OnClicked_Lambda([WeakComponent]()
		{
			if (WeakComponent.IsValid())
			{
				FPlatformApplicationMisc::ClipboardCopy(*WeakComponent->ToDisplayString());
			}
			return FReply::Handled();
		})
		[
			SNew(STextBlock)
			.Text(LOCTEXT("CopyAll", "Copy All to Clipboard"))
			.Font(IDetailLayoutBuilder::GetDetailFont())
		]
	];
}

void FFragmentsMetadataDetails::BuildIdentityRows(IDetailCategoryBuilder& Category, const FFragItemMetadata& Item)
{
	FillTextRow(Category.AddCustomRow(LOCTEXT("CategoryFilter", "IFC Class")), TEXT("IFC Class"), Item.Category);
	FillTextRow(Category.AddCustomRow(LOCTEXT("NameFilter", "Name")), TEXT("Name"), Item.Name);

	if (!Item.GUID.IsEmpty())
	{
		FillTextRow(Category.AddCustomRow(LOCTEXT("GuidFilter", "Global Id")), TEXT("Global Id"), Item.GUID);
	}

	if (!Item.TypeName.IsEmpty())
	{
		FillTextRow(Category.AddCustomRow(LOCTEXT("TypeFilter", "Type")), TEXT("Type"), Item.TypeName,
			FString::Printf(TEXT("%s\n\nIfcTypeObject, local id %d"), *Item.TypeName, Item.TypeLocalId));
	}

	if (!Item.ContainerName.IsEmpty() || Item.ContainerLocalId != INDEX_NONE)
	{
		const FString Container = Item.ContainerName.IsEmpty() ? Item.ContainerCategory : Item.ContainerName;
		FillTextRow(Category.AddCustomRow(LOCTEXT("ContainerFilter", "Contained In")), TEXT("Contained In"), Container,
			FString::Printf(TEXT("%s (%s), local id %d"), *Container, *Item.ContainerCategory, Item.ContainerLocalId));
	}

	if (!Item.StoreyName.IsEmpty() && Item.StoreyLocalId != Item.ContainerLocalId)
	{
		FillTextRow(Category.AddCustomRow(LOCTEXT("StoreyFilter", "Storey")), TEXT("Storey"), Item.StoreyName,
			FString::Printf(TEXT("Building storey '%s', local id %d"), *Item.StoreyName, Item.StoreyLocalId));
	}

	if (Item.LocalId != INDEX_NONE)
	{
		FillTextRow(
			Category.AddCustomRow(LOCTEXT("IdFilter", "Local Id")),
			TEXT("Local Id"),
			FString::Printf(TEXT("%d"), Item.LocalId),
			FString::Printf(TEXT("File-local id %d, IFC express id #%lld"), Item.LocalId, Item.ExpressId));
	}
}

void FFragmentsMetadataDetails::BuildMaterialRows(IDetailCategoryBuilder& Category, const FFragItemMetadata& Item)
{
	if (Item.Materials.Num() == 0)
	{
		return;
	}

	IDetailGroup& Group = Category.AddGroup(
		TEXT("Materials"),
		FText::Format(LOCTEXT("MaterialsGroup", "Materials ({0})"), FText::AsNumber(Item.Materials.Num())),
		/*bForAdvanced*/ false,
		/*bStartExpanded*/ false);

	const int32 ShownMaterials = FMath::Min(Item.Materials.Num(), MaxRowsPerGroup);
	for (int32 MaterialIndex = 0; MaterialIndex < ShownMaterials; ++MaterialIndex)
	{
		const FFragMaterial& Material = Item.Materials[MaterialIndex];

		FString Value = Material.Name;
		if (Material.Thickness > 0.0f)
		{
			Value += FString::Printf(TEXT("  —  %g thick"), Material.Thickness);
		}

		const FString Tooltip = Material.LayerSetName.IsEmpty()
			? Material.Name
			: FString::Printf(TEXT("%s\n\nLayer set: %s"), *Material.Name, *Material.LayerSetName);

		const FString RowName = Material.LayerSetName.IsEmpty() ? TEXT("Material") : Material.LayerSetName;
		FillTextRow(Group.AddWidgetRow(), RowName, Value, Tooltip);
	}

	if (Item.Materials.Num() > ShownMaterials)
	{
		FillOverflowRow(Group.AddWidgetRow(), Item.Materials.Num() - ShownMaterials);
	}
}

void FFragmentsMetadataDetails::BuildClassificationRows(IDetailCategoryBuilder& Category, const FFragItemMetadata& Item)
{
	if (Item.Classifications.Num() == 0)
	{
		return;
	}

	IDetailGroup& Group = Category.AddGroup(
		TEXT("Classification"),
		FText::Format(LOCTEXT("ClassificationGroup", "Classification ({0})"), FText::AsNumber(Item.Classifications.Num())),
		/*bForAdvanced*/ false,
		/*bStartExpanded*/ false);

	const int32 ShownClassifications = FMath::Min(Item.Classifications.Num(), MaxRowsPerGroup);
	for (int32 i = 0; i < ShownClassifications; ++i)
	{
		const FFragAttribute& Classification = Item.Classifications[i];
		FillTextRow(Group.AddWidgetRow(), Classification.Name, Classification.Value, MakeValueTooltip(Classification));
	}

	if (Item.Classifications.Num() > ShownClassifications)
	{
		FillOverflowRow(Group.AddWidgetRow(), Item.Classifications.Num() - ShownClassifications);
	}
}

void FFragmentsMetadataDetails::BuildAttributeRows(IDetailCategoryBuilder& Category, const FFragItemMetadata& Item)
{
	if (Item.Attributes.Num() == 0)
	{
		return;
	}

	IDetailGroup& Group = Category.AddGroup(
		TEXT("Attributes"),
		FText::Format(LOCTEXT("AttributesGroup", "Attributes ({0})"), FText::AsNumber(Item.Attributes.Num())),
		/*bForAdvanced*/ false,
		/*bStartExpanded*/ true);

	const int32 ShownAttributes = FMath::Min(Item.Attributes.Num(), MaxRowsPerGroup);
	for (int32 i = 0; i < ShownAttributes; ++i)
	{
		const FFragAttribute& Attribute = Item.Attributes[i];
		FillTextRow(Group.AddWidgetRow(), Attribute.Name, Attribute.Value, MakeValueTooltip(Attribute));
	}

	if (Item.Attributes.Num() > ShownAttributes)
	{
		FillOverflowRow(Group.AddWidgetRow(), Item.Attributes.Num() - ShownAttributes);
	}
}

void FFragmentsMetadataDetails::BuildPropertySetRows(IDetailCategoryBuilder& Category, const FFragItemMetadata& Item)
{
	int32 SetIndex = 0;

	const int32 ShownSets = FMath::Min(Item.PropertySets.Num(), MaxPropertySetGroups);
	for (int32 SetSlot = 0; SetSlot < ShownSets; ++SetSlot)
	{
		const FFragPropertySet& Set = Item.PropertySets[SetSlot];

		// Group names must be unique within a category; property set names are not.
		const FName GroupName(*FString::Printf(TEXT("Pset_%d"), SetIndex++));

		const FText Title = Set.bFromType
			? FText::Format(LOCTEXT("PsetGroupType", "{0} ({1}, from type)"),
				FText::FromString(Set.Name), FText::AsNumber(Set.Properties.Num()))
			: FText::Format(LOCTEXT("PsetGroup", "{0} ({1})"),
				FText::FromString(Set.Name), FText::AsNumber(Set.Properties.Num()));

		IDetailGroup& Group = Category.AddGroup(GroupName, Title, /*bForAdvanced*/ false, /*bStartExpanded*/ false);

		const int32 ShownProperties = FMath::Min(Set.Properties.Num(), MaxRowsPerGroup);
		for (int32 i = 0; i < ShownProperties; ++i)
		{
			const FFragAttribute& Property = Set.Properties[i];
			FillTextRow(Group.AddWidgetRow(), Property.Name, Property.Value, MakeValueTooltip(Property));
		}

		if (Set.Properties.Num() > ShownProperties)
		{
			FillOverflowRow(Group.AddWidgetRow(), Set.Properties.Num() - ShownProperties);
		}
	}

	if (Item.PropertySets.Num() > ShownSets)
	{
		Category.AddCustomRow(LOCTEXT("MorePsetsFilter", "Property Sets"))
		.WholeRowContent()
		[
			SNew(STextBlock)
			.Text(FText::Format(
				LOCTEXT("MorePsets", "… {0} more property set(s) not shown — use Copy All to Clipboard."),
				FText::AsNumber(Item.PropertySets.Num() - ShownSets)))
			.Font(IDetailLayoutBuilder::GetDetailFontItalic())
		];
	}
}

void FFragmentsMetadataDetails::BuildRelationRows(IDetailCategoryBuilder& Category, const FFragItemMetadata& Item)
{
	if (Item.Relations.Num() == 0)
	{
		return;
	}

	IDetailGroup& Group = Category.AddGroup(
		TEXT("Relations"),
		FText::Format(LOCTEXT("RelationsGroup", "Relations ({0})"), FText::AsNumber(Item.Relations.Num())),
		/*bForAdvanced*/ true,
		/*bStartExpanded*/ false);

	const int32 ShownRelations = FMath::Min(Item.Relations.Num(), MaxRowsPerGroup);
	for (int32 RelationIndex = 0; RelationIndex < ShownRelations; ++RelationIndex)
	{
		const FFragRelation& Relation = Item.Relations[RelationIndex];

		FString Targets;
		const int32 ShownCount = FMath::Min(Relation.RelatedLocalIds.Num(), 12);
		for (int32 i = 0; i < ShownCount; i++)
		{
			Targets += (i > 0 ? TEXT(", ") : TEXT(""));
			Targets += FString::FromInt(Relation.RelatedLocalIds[i]);
		}
		if (Relation.RelatedLocalIds.Num() > ShownCount)
		{
			Targets += FString::Printf(TEXT(", … (+%d)"), Relation.RelatedLocalIds.Num() - ShownCount);
		}

		FillTextRow(Group.AddWidgetRow(), Relation.Name, Targets,
			FString::Printf(TEXT("%d related item(s), by file-local id"), Relation.RelatedLocalIds.Num()));
	}

	if (Item.Relations.Num() > ShownRelations)
	{
		FillOverflowRow(Group.AddWidgetRow(), Item.Relations.Num() - ShownRelations);
	}
}

#undef LOCTEXT_NAMESPACE

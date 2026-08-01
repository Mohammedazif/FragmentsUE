// Copyright (c) 2026 Mohammed Azif. Licensed under the MIT License — see the LICENSE file.

#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"

class IDetailCategoryBuilder;
class IDetailGroup;
class UFragmentsMetadataComponent;
struct FFragItemMetadata;

/**
 * Renders IFC metadata as a readable section of the Details panel.
 *
 * Registered for the metadata component itself and for the actor classes that
 * own one, so selecting an imported element in the viewport immediately shows
 * its attributes and property sets instead of a raw struct dump.
 */
class FFragmentsMetadataDetails : public IDetailCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance();

	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

private:
	/** Find the metadata component behind whatever object is being customized. */
	static UFragmentsMetadataComponent* ResolveComponent(const TArray<TWeakObjectPtr<UObject>>& Objects);

	static void BuildIdentityRows(IDetailCategoryBuilder& Category, const FFragItemMetadata& Item);
	static void BuildAttributeRows(IDetailCategoryBuilder& Category, const FFragItemMetadata& Item);
	static void BuildPropertySetRows(IDetailCategoryBuilder& Category, const FFragItemMetadata& Item);
	static void BuildMaterialRows(IDetailCategoryBuilder& Category, const FFragItemMetadata& Item);
	static void BuildClassificationRows(IDetailCategoryBuilder& Category, const FFragItemMetadata& Item);
	static void BuildRelationRows(IDetailCategoryBuilder& Category, const FFragItemMetadata& Item);
};

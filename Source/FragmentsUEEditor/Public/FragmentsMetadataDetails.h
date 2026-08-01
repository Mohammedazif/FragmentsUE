// Copyright (c) 2026 Mohammed Azif. Licensed under the MIT License — see the LICENSE file.

#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"

class IDetailCategoryBuilder;
class IDetailGroup;
class UFragmentsMetadataComponent;
struct FFragItemMetadata;

class FFragmentsMetadataDetails : public IDetailCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance();

	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

private:
	static UFragmentsMetadataComponent* ResolveComponent(const TArray<TWeakObjectPtr<UObject>>& Objects);

	static void BuildIdentityRows(IDetailCategoryBuilder& Category, const FFragItemMetadata& Item);
	static void BuildAttributeRows(IDetailCategoryBuilder& Category, const FFragItemMetadata& Item);
	static void BuildPropertySetRows(IDetailCategoryBuilder& Category, const FFragItemMetadata& Item);
	static void BuildMaterialRows(IDetailCategoryBuilder& Category, const FFragItemMetadata& Item);
	static void BuildClassificationRows(IDetailCategoryBuilder& Category, const FFragItemMetadata& Item);
	static void BuildRelationRows(IDetailCategoryBuilder& Category, const FFragItemMetadata& Item);
};

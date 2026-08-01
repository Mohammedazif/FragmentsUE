// Copyright (c) 2026 Mohammed Azif. Licensed under the MIT License — see the LICENSE file.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "FragMetadata.h"
#include "FragmentsMetadataComponent.generated.h"

UCLASS(ClassGroup = (FragmentsUE), meta = (BlueprintSpawnableComponent, DisplayName = "IFC Metadata"))
class FRAGMENTSUE_API UFragmentsMetadataComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UFragmentsMetadataComponent();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IFC Metadata")
	FFragItemMetadata ItemData;

	UFUNCTION(BlueprintPure, Category = "IFC Metadata")
	FFragItemMetadata GetItemData() const { return ItemData; }

	UFUNCTION(BlueprintPure, Category = "IFC Metadata")
	FString GetGuid() const { return ItemData.GUID; }

	UFUNCTION(BlueprintPure, Category = "IFC Metadata")
	FString GetCategory() const { return ItemData.Category; }

	UFUNCTION(BlueprintPure, Category = "IFC Metadata")
	FString GetIfcName() const { return ItemData.Name; }

	UFUNCTION(BlueprintPure, Category = "IFC Metadata")
	int32 GetLocalId() const { return ItemData.LocalId; }

	UFUNCTION(BlueprintPure, Category = "IFC Metadata")
	bool GetAttribute(const FString& AttributeName, FString& OutValue) const;

	UFUNCTION(BlueprintPure, Category = "IFC Metadata")
	bool GetProperty(const FString& PropertySetName, const FString& PropertyName, FString& OutValue) const;

	UFUNCTION(BlueprintPure, Category = "IFC Metadata")
	TArray<FString> GetPropertySetNames() const;

	UFUNCTION(BlueprintPure, Category = "IFC Metadata")
	TMap<FString, FString> GetPropertiesInSet(const FString& PropertySetName) const;

	/** Attributes are keyed by name; properties are keyed "PsetName.PropertyName". */
	UFUNCTION(BlueprintPure, Category = "IFC Metadata")
	TMap<FString, FString> GetAllValuesFlat() const;

	UFUNCTION(BlueprintPure, Category = "IFC Metadata")
	TArray<FString> GetMaterialNames() const;

	UFUNCTION(BlueprintPure, Category = "IFC Metadata")
	FString GetTypeName() const { return ItemData.TypeName; }

	UFUNCTION(BlueprintPure, Category = "IFC Metadata")
	FString GetContainerName() const { return ItemData.ContainerName; }

	UFUNCTION(BlueprintPure, Category = "IFC Metadata")
	FString GetStoreyName() const { return ItemData.StoreyName; }

	UFUNCTION(BlueprintPure, Category = "IFC Metadata")
	TArray<int32> GetRelatedLocalIds(const FString& RelationName) const;

	UFUNCTION(BlueprintPure, Category = "IFC Metadata")
	FString ToDisplayString() const;
};

UCLASS()
class FRAGMENTSUE_API UFragmentsMetadataLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category = "FragmentsUE|Metadata", meta = (DefaultToSelf = "Actor"))
	static UFragmentsMetadataComponent* FindMetadataComponent(AActor* Actor, bool bSearchAttachParents = true);

	UFUNCTION(BlueprintPure, Category = "FragmentsUE|Metadata", meta = (DefaultToSelf = "Actor"))
	static bool GetActorMetadata(AActor* Actor, FFragItemMetadata& OutItem);
};

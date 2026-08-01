// Copyright (c) 2026 Mohammed Azif. Licensed under the MIT License — see the LICENSE file.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Engine/StaticMeshActor.h"
#include "FragMetadata.h"
#include "FragmentsElementActor.generated.h"

class UFragmentsMetadataComponent;

UCLASS(NotPlaceable, DisplayName = "Fragments Element")
class FRAGMENTSUE_API AFragmentsElementActor : public AStaticMeshActor
{
	GENERATED_BODY()

public:
	AFragmentsElementActor(const FObjectInitializer& ObjectInitializer);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IFC Metadata")
	TObjectPtr<UFragmentsMetadataComponent> MetadataComponent;

	void SetItemMetadata(const FFragItemMetadata& InItem);
};

UCLASS(NotPlaceable, DisplayName = "Fragments Spatial Node")
class FRAGMENTSUE_API AFragmentsNodeActor : public AActor
{
	GENERATED_BODY()

public:
	AFragmentsNodeActor(const FObjectInitializer& ObjectInitializer);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IFC Metadata")
	TObjectPtr<UFragmentsMetadataComponent> MetadataComponent;

	void SetItemMetadata(const FFragItemMetadata& InItem);
};

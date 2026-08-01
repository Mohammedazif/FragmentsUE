// Copyright (c) 2026 Mohammed Azif. Licensed under the MIT License — see the LICENSE file.

#pragma once

#include "CoreMinimal.h"
#include "ActorFactories/ActorFactory.h"
#include "ActorFactoryFragmentsModel.generated.h"

/**
 * Factory that spawns an AFragmentsActor when a UFragmentsModelAsset is dragged into the viewport.
 */
UCLASS()
class FRAGMENTSUEEDITOR_API UActorFactoryFragmentsModel : public UActorFactory
{
	GENERATED_BODY()

public:
	UActorFactoryFragmentsModel();

	virtual bool CanCreateActorFrom(const FAssetData& AssetData, FText& OutErrorMsg) override;
	virtual void PostSpawnActor(UObject* Asset, AActor* NewActor) override;
	virtual FString GetDefaultActorLabel(UObject* Asset) const override;
};

// Copyright (c) 2026 Mohammed Azif. Licensed under the MIT License — see the LICENSE file.

#include "FragmentsElementActor.h"
#include "FragmentsMetadataComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SceneComponent.h"

AFragmentsElementActor::AFragmentsElementActor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryActorTick.bCanEverTick = false;

	MetadataComponent = CreateDefaultSubobject<UFragmentsMetadataComponent>(TEXT("IFCMetadata"));

	if (UStaticMeshComponent* MeshComponent = GetStaticMeshComponent())
	{
		MeshComponent->SetMobility(EComponentMobility::Static);
	}
}

void AFragmentsElementActor::SetItemMetadata(const FFragItemMetadata& InItem)
{
	if (MetadataComponent)
	{
		MetadataComponent->ItemData = InItem;
	}
}

AFragmentsNodeActor::AFragmentsNodeActor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryActorTick.bCanEverTick = false;

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Root->SetMobility(EComponentMobility::Static);
	RootComponent = Root;

	MetadataComponent = CreateDefaultSubobject<UFragmentsMetadataComponent>(TEXT("IFCMetadata"));
}

void AFragmentsNodeActor::SetItemMetadata(const FFragItemMetadata& InItem)
{
	if (MetadataComponent)
	{
		MetadataComponent->ItemData = InItem;
	}
}

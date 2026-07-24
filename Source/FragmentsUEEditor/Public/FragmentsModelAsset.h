// Copyright Azif. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "FragmentsModelAsset.generated.h"

/**
 * A dummy asset used purely to facilitate drag-and-drop from the OS into the Unreal Editor.
 * When a .frag file is dropped, the factory creates this asset temporarily.
 * When this asset is dropped into the viewport, the ActorFactory reads its FilePath and spawns the FragmentsActor.
 */
UCLASS(BlueprintType)
class FRAGMENTSUEEDITOR_API UFragmentsModelAsset : public UObject
{
	GENERATED_BODY()

public:
	/** The absolute path to the .frag file on disk */
	UPROPERTY(VisibleAnywhere, Category = "FragmentsUE")
	FString SourceFilePath;
};

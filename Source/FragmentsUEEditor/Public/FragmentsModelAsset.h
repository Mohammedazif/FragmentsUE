// Copyright (c) 2026 Mohammed Azif. Licensed under the MIT License — see the LICENSE file.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "FragImportOptions.h"
#include "FragmentsModelAsset.generated.h"

UCLASS(BlueprintType)
class FRAGMENTSUEEDITOR_API UFragmentsModelAsset : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY(VisibleAnywhere, Category = "FragmentsUE")
	FString SourceFilePath;

	UPROPERTY(EditAnywhere, Category = "FragmentsUE")
	FFragImportOptions ImportOptions;
};

// Copyright (c) 2026 Mohammed Azif. Licensed under the MIT License — see the LICENSE file.

#pragma once

#include "CoreMinimal.h"
#include "Factories/Factory.h"
#include "FragmentsModelAssetFactory.generated.h"

/**
 * Factory for importing .frag files into the editor.
 * It creates a UFragmentsModelAsset which just holds the file path.
 */
UCLASS()
class FRAGMENTSUEEDITOR_API UFragmentsModelAssetFactory : public UFactory
{
	GENERATED_BODY()

public:
	UFragmentsModelAssetFactory();

	virtual UObject* FactoryCreateFile(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, const FString& Filename, const TCHAR* Parms, FFeedbackContext* Warn, bool& bOutOperationCanceled) override;
};

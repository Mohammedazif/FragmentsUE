// Copyright (c) 2026 Mohammed Azif. Licensed under the MIT License — see the LICENSE file.

#include "FragmentsModelAssetFactory.h"
#include "FragmentsModelAsset.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

UFragmentsModelAssetFactory::UFragmentsModelAssetFactory()
{
	SupportedClass = UFragmentsModelAsset::StaticClass();
	
	// Add .frag extension
	Formats.Add(TEXT("frag;Fragments Model"));

	bCreateNew = false;
	bEditorImport = true;
	bText = false;
}

UObject* UFragmentsModelAssetFactory::FactoryCreateFile(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, const FString& Filename, const TCHAR* Parms, FFeedbackContext* Warn, bool& bOutOperationCanceled)
{
	UFragmentsModelAsset* NewAsset = NewObject<UFragmentsModelAsset>(InParent, InClass, InName, Flags);
	if (NewAsset)
	{
		NewAsset->SourceFilePath = IFileManager::Get().ConvertToAbsolutePathForExternalAppForRead(*Filename);
	}
	return NewAsset;
}

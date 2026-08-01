// Copyright (c) 2026 Mohammed Azif. Licensed under the MIT License — see the LICENSE file.

#include "FragAssetFactory.h"
#include "FragmentsUEModule.h"

#include "UObject/Package.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"

#if WITH_EDITOR
#include "AssetRegistry/AssetRegistryModule.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialInterface.h"
#include "UObject/SavePackage.h"
#endif

namespace
{
	/** CreatePackage builds an FName, which is a fatal check past 1023 characters. */
	constexpr int32 MaxAssetNameChars = 64;

	FString SanitizeAssetName(const FString& In)
	{
		FString Out;
		Out.Reserve(FMath::Min(In.Len(), MaxAssetNameChars));

		const int32 CopyLen = FMath::Min(In.Len(), MaxAssetNameChars);
		for (int32 i = 0; i < CopyLen; ++i)
		{
			const TCHAR C = In[i];
			Out.AppendChar((FChar::IsAlnum(C) || C == TEXT('_')) ? C : TEXT('_'));
		}

		if (Out.IsEmpty())
		{
			Out = TEXT("Asset");
		}

		// Prefixed before the clamp so it cannot push the result back over the limit.
		if (FChar::IsDigit(Out[0]))
		{
			Out.InsertAt(0, TEXT('A'));
		}

		if (Out.Len() > MaxAssetNameChars)
		{
			Out = Out.Left(MaxAssetNameChars - 9) + FString::Printf(TEXT("_%08x"), FCrc::StrCrc32(*In));
		}

		return Out;
	}
}

FFragAssetFactory::FFragAssetFactory(const FString& InRootPath, const FString& InModelName)
{
#if WITH_EDITOR
	FString Root = InRootPath.TrimStartAndEnd();
	if (Root.IsEmpty())
	{
		Root = TEXT("/Game/Fragments");
	}
	if (!Root.StartsWith(TEXT("/")))
	{
		Root = TEXT("/") + Root;
	}
	Root.RemoveFromEnd(TEXT("/"));

	const FString Model = SanitizeAssetName(InModelName.IsEmpty() ? TEXT("Model") : InModelName);
	BasePath = Root / Model;

	// CreatePackage FNames the whole path; IsValidLongPackageName checks structure, not length.
	if (BasePath.Len() > 200)
	{
		UE_LOG(LogFragmentsUE, Error,
			TEXT("Asset path is %d characters, which leaves no room for asset names — falling back to transient objects."),
			BasePath.Len());
		bValid = false;
		return;
	}

	if (!FPackageName::IsValidLongPackageName(BasePath / TEXT("Probe"), /*bIncludeReadOnlyRoots*/ false))
	{
		UE_LOG(LogFragmentsUE, Error,
			TEXT("Asset path '%s' is not a valid content path — falling back to transient objects. Use something under /Game/."),
			*BasePath);
		bValid = false;
		return;
	}

	bValid = true;
#else
	bValid = false;
#endif
}

bool FFragAssetFactory::PackageAlreadyExists(const FString& SubFolder, const FString& Candidate) const
{
#if WITH_EDITOR
	const FString PackageName = BasePath / SubFolder / Candidate;
	return FPackageName::DoesPackageExist(PackageName) || FindPackage(nullptr, *PackageName) != nullptr;
#else
	return false;
#endif
}

FString FFragAssetFactory::MakeUniqueAssetName(const FString& SubFolder, const FString& DesiredName)
{
	const FString Sanitized = SanitizeAssetName(DesiredName);
	FString Candidate = Sanitized;

	// Reusing an existing package name reinitialises the live object in place: render-thread crash.
	int32 Suffix = 1;
	while (UsedNames.Contains(SubFolder / Candidate) || PackageAlreadyExists(SubFolder, Candidate))
	{
		Candidate = FString::Printf(TEXT("%s_%d"), *Sanitized, Suffix++);
	}

	UsedNames.Add(SubFolder / Candidate);
	return Candidate;
}

UPackage* FFragAssetFactory::CreateAssetPackage(const FString& SubFolder, const FString& AssetName, FString& OutAssetName)
{
#if WITH_EDITOR
	if (!bValid)
	{
		return nullptr;
	}

	OutAssetName = MakeUniqueAssetName(SubFolder, AssetName);

	const FString PackageName = BasePath / SubFolder / OutAssetName;

	// CreatePackage FNames this; a malformed or over-long path is a fatal check, not a failure.
	if (!FPackageName::IsValidLongPackageName(PackageName, /*bIncludeReadOnlyRoots*/ false))
	{
		UE_LOG(LogFragmentsUE, Warning, TEXT("Refusing to create package with invalid name %s"), *PackageName);
		return nullptr;
	}

	UPackage* Package = CreatePackage(*PackageName);
	if (!Package)
	{
		UE_LOG(LogFragmentsUE, Warning, TEXT("Could not create package %s"), *PackageName);
		return nullptr;
	}

	Package->FullyLoad();
	return Package;
#else
	return nullptr;
#endif
}

void FFragAssetFactory::RegisterAsset(UObject* Asset)
{
#if WITH_EDITOR
	if (!bValid || !Asset)
	{
		return;
	}

	UPackage* Package = Asset->GetOutermost();
	if (!Package || Package == GetTransientPackage())
	{
		return;
	}

	Asset->SetFlags(RF_Public | RF_Standalone);
	FAssetRegistryModule::AssetCreated(Asset);
	Package->MarkPackageDirty();

	CreatedAssets.Add({ Package, Asset });
#endif
}

UMaterialInterface* FFragAssetFactory::CreateMaterialInstance(
	UMaterialInterface* Parent,
	const FString& AssetName,
	const FLinearColor& Color,
	float Opacity,
	bool bIsGlass)
{
#if WITH_EDITOR
	if (!bValid || !Parent)
	{
		return nullptr;
	}

	FString ObjectName;
	UPackage* Package = CreateAssetPackage(TEXT("Materials"), AssetName, ObjectName);
	if (!Package)
	{
		return nullptr;
	}

	UMaterialInstanceConstant* Instance = NewObject<UMaterialInstanceConstant>(
		Package, *ObjectName, RF_Public | RF_Standalone);
	if (!Instance)
	{
		return nullptr;
	}

	Instance->SetParentEditorOnly(Parent);

	Instance->SetVectorParameterValueEditorOnly(FMaterialParameterInfo(TEXT("BaseColor")), Color);
	Instance->SetVectorParameterValueEditorOnly(FMaterialParameterInfo(TEXT("Color")), Color);
	Instance->SetScalarParameterValueEditorOnly(FMaterialParameterInfo(TEXT("Opacity")), Opacity);

	if (!bIsGlass)
	{
		Instance->SetScalarParameterValueEditorOnly(FMaterialParameterInfo(TEXT("Roughness")), 0.65f);
		Instance->SetScalarParameterValueEditorOnly(FMaterialParameterInfo(TEXT("Specular")), 0.4f);
	}

	Instance->PostEditChange();
	RegisterAsset(Instance);

	return Instance;
#else
	return nullptr;
#endif
}

int32 FFragAssetFactory::SaveAll()
{
#if WITH_EDITOR
	if (!bValid || CreatedAssets.Num() == 0)
	{
		return 0;
	}

	int32 SavedCount = 0;

	for (const FCreatedAsset& Created : CreatedAssets)
	{
		UPackage* Package = Created.Package.Get();
		UObject* Asset = Created.Asset.Get();
		if (!Package || !Asset)
		{
			continue;
		}

		const FString FileName = FPackageName::LongPackageNameToFilename(
			Package->GetName(), FPackageName::GetAssetPackageExtension());

		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.Error = GWarn;
		SaveArgs.bForceByteSwapping = false;
		SaveArgs.bWarnOfLongFilename = false;

		if (UPackage::SavePackage(Package, Asset, *FileName, SaveArgs))
		{
			SavedCount++;
		}
		else
		{
			UE_LOG(LogFragmentsUE, Warning, TEXT("Failed to save %s"), *Package->GetName());
		}
	}

	return SavedCount;
#else
	return 0;
#endif
}

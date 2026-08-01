// Copyright (c) 2026 Mohammed Azif. Licensed under the MIT License — see the LICENSE file.

#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtr.h"

class UPackage;
class UMaterialInterface;

/** Editor only: outside the editor every call is a no-op and IsValid() is false. */
class FRAGMENTSUE_API FFragAssetFactory
{
public:
	FFragAssetFactory(const FString& InRootPath, const FString& InModelName);

	bool IsValid() const { return bValid; }

	const FString& GetBasePath() const { return BasePath; }

	UPackage* CreateAssetPackage(const FString& SubFolder, const FString& AssetName, FString& OutAssetName);

	void RegisterAsset(UObject* Asset);

	UMaterialInterface* CreateMaterialInstance(
		UMaterialInterface* Parent,
		const FString& AssetName,
		const FLinearColor& Color,
		float Opacity,
		bool bIsGlass);

	int32 SaveAll();

	int32 GetAssetCount() const { return CreatedAssets.Num(); }

private:
	FString MakeUniqueAssetName(const FString& SubFolder, const FString& DesiredName);

	bool PackageAlreadyExists(const FString& SubFolder, const FString& Candidate) const;

	FString BasePath;
	bool bValid = false;

	struct FCreatedAsset
	{
		TWeakObjectPtr<UPackage> Package;
		TWeakObjectPtr<UObject> Asset;
	};

	TArray<FCreatedAsset> CreatedAssets;
	TSet<FString> UsedNames;
};

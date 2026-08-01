// Copyright (c) 2026 Mohammed Azif. Licensed under the MIT License — see the LICENSE file.

#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtr.h"

class UPackage;
class UMaterialInterface;

/**
 * Writes the objects generated during an import into the Content Browser as
 * real assets, so a level referencing them can be saved and cooked.
 *
 * This is the second stage of the import, mirroring how Datasmith finalises its
 * in-memory scene into Static Mesh and Material Instance assets. Without it,
 * meshes are transient objects owned by an actor and materials are dynamic
 * instances — neither survives packaging.
 *
 * Editor only. Outside the editor every call is a no-op and IsValid() is false,
 * which puts the caller back on the transient path used for runtime loading.
 */
class FRAGMENTSUE_API FFragAssetFactory
{
public:
	/**
	 * @param InRootPath   Content path root, e.g. "/Game/Fragments".
	 * @param InModelName  Model folder created under the root.
	 */
	FFragAssetFactory(const FString& InRootPath, const FString& InModelName);

	/** False when assets cannot be written — outside the editor, or a bad path. */
	bool IsValid() const { return bValid; }

	/** Content path the assets are being written to, for logging. */
	const FString& GetBasePath() const { return BasePath; }

	/**
	 * Create a package to hold one asset.
	 * @param SubFolder    "Geometry" or "Materials".
	 * @param AssetName    Desired name; sanitised and made unique.
	 * @param OutAssetName Receives the final object name to use.
	 * @return The package to pass as the new object's Outer, or null on failure.
	 */
	UPackage* CreateAssetPackage(const FString& SubFolder, const FString& AssetName, FString& OutAssetName);

	/** Flag a freshly created object as a saveable asset and tell the asset registry. */
	void RegisterAsset(UObject* Asset);

	/**
	 * Create a saved material instance carrying the same parameters the dynamic
	 * instances use, so a baked model shades identically to a runtime one.
	 */
	UMaterialInterface* CreateMaterialInstance(
		UMaterialInterface* Parent,
		const FString& AssetName,
		const FLinearColor& Color,
		float Opacity,
		bool bIsGlass);

	/** Write every package created so far to disk. @return number saved. */
	int32 SaveAll();

	/** How many assets have been created. */
	int32 GetAssetCount() const { return CreatedAssets.Num(); }

private:
	/** Strip characters that are illegal in a package name and make it unique. */
	FString MakeUniqueAssetName(const FString& SubFolder, const FString& DesiredName);

	/** True if this package is already on disk or already loaded. */
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

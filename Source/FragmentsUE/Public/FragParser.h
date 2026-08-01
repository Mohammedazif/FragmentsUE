// Copyright (c) 2026 Mohammed Azif. Licensed under the MIT License — see the LICENSE file.

#pragma once

#include "CoreMinimal.h"
#include "FragImportResult.h"
#include "FragImportOptions.h"

/** All output positions/transforms are in UE space: left-handed Z-up, centimetres. */
class FRAGMENTSUE_API FFragParser
{
public:
	static FFragImportResult LoadFromFile(
		const FString& FilePath,
		const FFragImportOptions& Options = FFragImportOptions());

	static FFragImportResult LoadFromBuffer(
		const TArray<uint8>& RawData,
		const FFragImportOptions& Options = FFragImportOptions());

private:
	/** Zlib-compressed payloads are identified by magic byte 0x78. */
	static bool TryDecompress(
		const TArray<uint8>& CompressedData,
		TArray<uint8>& OutDecompressed);

	static FFragImportResult ParseModel(
		const uint8* Buffer,
		int32 BufferSize,
		const FFragImportOptions& Options);

	/** Source is RH Y-up metres: (X, Y, Z) → (X * Scale, Z * Scale, Y * Scale). */
	static FVector ConvertPosition(float X, float Y, float Z, float Scale);
	static FVector ConvertPositionD(double X, double Y, double Z, float Scale);

	/** .frag stores only x_direction and y_direction; z_direction = cross(x, y). */
	static FTransform BuildTransform(
		double PosX, double PosY, double PosZ,
		float XDirX, float XDirY, float XDirZ,
		float YDirX, float YDirY, float YDirZ,
		float Scale);

	static void ComputeFlatNormals(FFragGeometry& Geometry);
};

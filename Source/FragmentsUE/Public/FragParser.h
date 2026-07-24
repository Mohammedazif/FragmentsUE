// Copyright Azif. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "FragImportResult.h"
#include "FragImportOptions.h"

/**
 * FFragParser — Core parser for ThatOpen .frag (Fragments 2.0) files.
 *
 * Reads FlatBuffers-serialized data, decompresses if needed, extracts geometry,
 * transforms, materials, GUIDs, categories, and spatial structure into
 * engine-agnostic FFragImportResult structs.
 *
 * All output positions/transforms are converted to UE coordinate space
 * (left-handed Z-up, centimeters).
 */
class FRAGMENTSUE_API FFragParser
{
public:
	/**
	 * Load and parse a .frag file from disk.
	 * @param FilePath   Absolute path to the .frag file
	 * @param Options    Import options (scale, mesh mode, etc.)
	 * @return           Parsed result with geometries, instances, and metadata
	 */
	static FFragImportResult LoadFromFile(
		const FString& FilePath,
		const FFragImportOptions& Options = FFragImportOptions());

	/**
	 * Parse from a raw byte buffer (for runtime loading from memory/network).
	 * @param RawData    Raw bytes of the .frag file (possibly compressed)
	 * @param Options    Import options
	 * @return           Parsed result
	 */
	static FFragImportResult LoadFromBuffer(
		const TArray<uint8>& RawData,
		const FFragImportOptions& Options = FFragImportOptions());

private:
	/** Check if data is zlib-compressed (magic byte 0x78) and decompress. */
	static bool TryDecompress(
		const TArray<uint8>& CompressedData,
		TArray<uint8>& OutDecompressed);

	/** Parse the FlatBuffers Model root and populate the result. */
	static FFragImportResult ParseModel(
		const uint8* Buffer,
		int32 BufferSize,
		const FFragImportOptions& Options);

	/**
	 * Convert a Fragments position (RH Y-up, meters) to UE (LH Z-up, cm).
	 * Transform: (X, Y, Z) → (X * Scale, Z * Scale, Y * Scale)
	 */
	static FVector ConvertPosition(float X, float Y, float Z, float Scale);
	static FVector ConvertPositionD(double X, double Y, double Z, float Scale);

	/**
	 * Build a UE FTransform from the Fragments Transform struct.
	 * Fragments stores position (DoubleVector) + x_direction + y_direction;
	 * z_direction = cross(x, y).
	 */
	static FTransform BuildTransform(
		double PosX, double PosY, double PosZ,
		float XDirX, float XDirY, float XDirZ,
		float YDirX, float YDirY, float YDirZ,
		float Scale);

	/** Compute flat (face) normals for a triangle mesh in-place. */
	static void ComputeFlatNormals(FFragGeometry& Geometry);
};

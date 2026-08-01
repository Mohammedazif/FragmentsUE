#pragma once

#include "CoreMinimal.h"
#include "FragImportResult.h"

class UStaticMesh;
class UProceduralMeshComponent;
class UMaterialInterface;
struct FMeshDescription;

/**
 * One element's contribution to a merged mesh: which geometry, placed where.
 * The transform is baked into the vertices, so the merged mesh needs no instancing.
 */
struct FFragMergePart
{
	const FFragGeometry* Geometry = nullptr;
	FTransform Transform = FTransform::Identity;
};

class FRAGMENTSUE_API FFragMeshBuilder
{
public:
	/**
	 * Weld many placed geometries into one UStaticMesh, baking each part's
	 * transform into its vertices.
	 *
	 * Each part's index is also written into UV channel 0 of its vertex instances.
	 * That mapping survives the build; the triangle order does not.
	 *
	 * @param OutPartTriangleStarts  If set, receives the first triangle index of
	 *                               each part, in the order the parts were given.
	 *                               Only usable when the build preserved that order —
	 *                               see OutTriangleOrderPreserved.
	 * @param OutTriangleOrderPreserved  If set, receives whether the built mesh's
	 *                               triangles are still in emission order. False for
	 *                               the editor build, which runs a vertex-cache
	 *                               optimiser that permutes them; a caller that
	 *                               ignores this resolves faces to the wrong element.
	 */
	static UStaticMesh* BuildMergedStaticMesh(
		UObject* Outer,
		const TArray<FFragMergePart>& Parts,
		const FName& MeshName,
		const FLinearColor& Color,
		float Opacity = 1.0f,
		UMaterialInterface* Material = nullptr,
		TArray<int32>* OutPartTriangleStarts = nullptr,
		bool* OutTriangleOrderPreserved = nullptr,
		bool bCookCollision = true);

	/** Decode the part index this builder wrote into UV channel 0. */
	static int32 DecodePartIndexFromUV(const FVector2D& UV);

	/** 
	 * Build a UStaticMesh from parsed geometry data.
	 * Can be used at runtime (via fast build path).
	 */
	static UStaticMesh* BuildStaticMesh(UObject* Outer, const FFragGeometry& Geometry, const FName& MeshName, const FLinearColor& Color, float Opacity = 1.0f, UMaterialInterface* Material = nullptr, bool bCookCollision = true);

	/** 
	 * Create a Procedural Mesh Component from parsed geometry data.
	 */
	static void BuildProceduralMesh(const FFragGeometry& Geometry, UProceduralMeshComponent* ProcMeshComp, const FLinearColor& Color, float Opacity = 1.0f, bool bCreateCollision = false);

private:
	/** Helper to convert FFragGeometry to FMeshDescription */
	static void PopulateMeshDescription(const FFragGeometry& Geometry, FMeshDescription& OutMeshDesc, const FLinearColor& Color, float Opacity = 1.0f);

	/** Same, for many transformed parts appended into one description. Returns the
	 *  number of triangles actually emitted, which is zero when every part was
	 *  rejected. */
	static int32 PopulateMergedMeshDescription(
		const TArray<FFragMergePart>& Parts,
		FMeshDescription& OutMeshDesc,
		const FLinearColor& Color,
		float Opacity,
		TArray<int32>* OutPartTriangleStarts);
};

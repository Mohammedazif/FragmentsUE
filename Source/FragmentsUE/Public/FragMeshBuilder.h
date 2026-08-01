#pragma once

#include "CoreMinimal.h"
#include "FragImportResult.h"

class UStaticMesh;
class UProceduralMeshComponent;
class UMaterialInterface;
struct FMeshDescription;

struct FFragMergePart
{
	const FFragGeometry* Geometry = nullptr;
	FTransform Transform = FTransform::Identity;
};

class FRAGMENTSUE_API FFragMeshBuilder
{
public:
	/** Welds placed geometries into one static mesh; part index is encoded in UV0. */
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

	static int32 DecodePartIndexFromUV(const FVector2D& UV);

	static UStaticMesh* BuildStaticMesh(UObject* Outer, const FFragGeometry& Geometry, const FName& MeshName, const FLinearColor& Color, float Opacity = 1.0f, UMaterialInterface* Material = nullptr, bool bCookCollision = true);

	static void BuildProceduralMesh(const FFragGeometry& Geometry, UProceduralMeshComponent* ProcMeshComp, const FLinearColor& Color, float Opacity = 1.0f, bool bCreateCollision = false);

private:
	static void PopulateMeshDescription(const FFragGeometry& Geometry, FMeshDescription& OutMeshDesc, const FLinearColor& Color, float Opacity = 1.0f);

	static int32 PopulateMergedMeshDescription(
		const TArray<FFragMergePart>& Parts,
		FMeshDescription& OutMeshDesc,
		const FLinearColor& Color,
		float Opacity,
		TArray<int32>* OutPartTriangleStarts);
};

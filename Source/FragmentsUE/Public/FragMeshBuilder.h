#pragma once

#include "CoreMinimal.h"
#include "FragImportResult.h"

class UStaticMesh;
class UProceduralMeshComponent;
struct FMeshDescription;

class FRAGMENTSUE_API FFragMeshBuilder
{
public:
	/** 
	 * Build a UStaticMesh from parsed geometry data.
	 * Can be used at runtime (via fast build path).
	 */
	static UStaticMesh* BuildStaticMesh(
		const FFragGeometry& Geometry, 
		UObject* Outer, 
		const FName& MeshName);

	/** 
	 * Create a Procedural Mesh Component from parsed geometry data.
	 */
	static void BuildProceduralMesh(
		const FFragGeometry& Geometry, 
		UProceduralMeshComponent* ProcMeshComp);

private:
	/** Helper to convert FFragGeometry to FMeshDescription */
	static void PopulateMeshDescription(const FFragGeometry& Geometry, FMeshDescription& OutMeshDesc);
};

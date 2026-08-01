#include "FragMeshBuilder.h"
#include "FragmentsUEModule.h"
#include "Engine/StaticMesh.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
#include "ProceduralMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "UObject/Package.h"
#include "PhysicsEngine/BodySetup.h"

namespace
{
	static const FName GMaterialSlotName("Material_0");

	/**
	 * Ceiling on one merged mesh description. Both counts are file-controlled and the
	 * caller chunks on vertex count alone, which does not bound the work: a shell whose
	 * faces all reference the same few points dedups to a handful of vertices while
	 * carrying millions of indices, and every instance of it re-adds the whole index
	 * list. Each index becomes a vertex instance and every three a polygon, both sparse
	 * array elements costing on the order of a hundred bytes.
	 */
	static constexpr int64 GMaxMergedVertexInstances = 8 * 1000 * 1000;

	/**
	 * Companion ceiling on vertices. Charging indices alone leaves the other door
	 * open: a part with fifty million positions and six indices costs nothing against
	 * an instance budget, and the caller cannot split it either, since it only ever
	 * flushes *between* parts.
	 */
	static constexpr int64 GMaxMergedDescriptionVertices = 4 * 1000 * 1000;

	/**
	 * The part index is encoded into UV0 as two 10-bit halves, so it aliases past this
	 * many parts — and an aliased index decodes to a valid but wrong element, silently.
	 * The caller's chunking keeps parts per chunk far below it; this is the guard for
	 * when that stops being true, since the two limits live in different files.
	 */
	static constexpr int32 GMaxEncodableParts = 1 << 20;

	/**
	 * A part index is written into UV0 so an element stays identifiable after the
	 * editor build reorders triangles. It is split across the two components because a
	 * UV channel may be stored at half precision, where only integers up to 2048 are
	 * exact; both halves stay under 1024. All three corners of a triangle carry the
	 * same value, so barycentric interpolation at a hit returns it unchanged.
	 */
	FVector2f EncodePartIndexAsUV(int32 PartIndex)
	{
		return FVector2f(
			static_cast<float>(PartIndex & 1023),
			static_cast<float>((PartIndex >> 10) & 1023));
	}

	/**
	 * FStaticMaterial's imported-slot-name field only exists in editor builds,
	 * so the three-argument constructor does not compile when packaging.
	 */
	FStaticMaterial MakeStaticMaterial(UMaterialInterface* Material)
	{
#if WITH_EDITORONLY_DATA
		return FStaticMaterial(Material, GMaterialSlotName, GMaterialSlotName);
#else
		return FStaticMaterial(Material, GMaterialSlotName);
#endif
	}

	/**
	 * Source models carry the build settings the editor uses to bake a mesh.
	 * They do not exist in a packaged build, which goes through the fast build
	 * path and takes the mesh description as-is.
	 */
	void ConfigureSourceModel(UStaticMesh* StaticMesh, bool bHasNormals)
	{
#if WITH_EDITOR
		FStaticMeshSourceModel& SourceModel = StaticMesh->AddSourceModel();
		SourceModel.BuildSettings.bRecomputeNormals = !bHasNormals;  // only if ours are missing
		SourceModel.BuildSettings.bRecomputeTangents = true;         // always recompute from geometry
		SourceModel.BuildSettings.bUseMikkTSpace = false;            // our planar UVs give MikkTSpace degenerate bases
		SourceModel.BuildSettings.bRemoveDegenerates = false;        // keep thin IFC geometry
		SourceModel.BuildSettings.bUseHighPrecisionTangentBasis = false;
		SourceModel.BuildSettings.bUseFullPrecisionUVs = false;
		SourceModel.BuildSettings.bGenerateLightmapUVs = false;
		SourceModel.BuildSettings.SrcLightmapIndex = 0;
		SourceModel.BuildSettings.DstLightmapIndex = 1;
#endif
	}
}

UStaticMesh* FFragMeshBuilder::BuildStaticMesh(UObject* Outer, const FFragGeometry& Geometry, const FName& MeshName, const FLinearColor& Color, float Opacity, UMaterialInterface* Material, bool bCookCollision)
{
	if (Geometry.Positions.IsEmpty() || Geometry.Indices.IsEmpty())
	{
		return nullptr;
	}

	UStaticMesh* StaticMesh = NewObject<UStaticMesh>(Outer, MeshName, RF_Public | RF_Transactional | RF_Standalone);
	StaticMesh->InitResources();

	StaticMesh->SetLightingGuid(FGuid::NewGuid());

	// Set up source model
	const bool bHasNormals = Geometry.Normals.Num() > 0 && Geometry.Normals.Num() == Geometry.Positions.Num();
	ConfigureSourceModel(StaticMesh, bHasNormals);

	FMeshDescription MeshDesc;
	PopulateMeshDescription(Geometry, MeshDesc, Color, Opacity);

	TArray<const FMeshDescription*> MeshDescPtrs;
	MeshDescPtrs.Add(&MeshDesc);

	UStaticMesh::FBuildMeshDescriptionsParams BuildParams;
#if WITH_EDITOR
	BuildParams.bFastBuild = false; // Normal build generates collision properly in Editor
#else
	BuildParams.bFastBuild = true; // Fast path required at runtime in packaged builds
#endif
	BuildParams.bBuildSimpleCollision = false;

	StaticMesh->GetStaticMaterials().Add(MakeStaticMaterial(Material));

	// The trimesh cook is the most expensive part of importing a large model, and it
	// is only worth paying for when traces need to resolve which element was hit.
	if (bCookCollision)
	{
		if (!StaticMesh->GetBodySetup())
		{
			StaticMesh->CreateBodySetup();
		}
		if (UBodySetup* BodySetup = StaticMesh->GetBodySetup())
		{
			BodySetup->CollisionTraceFlag = CTF_UseComplexAsSimple;
			BodySetup->bDoubleSidedGeometry = true;
			BodySetup->bMeshCollideAll = true;
		}
	}

	StaticMesh->BuildFromMeshDescriptions(MeshDescPtrs, BuildParams);

	if (bCookCollision)
	{
		if (UBodySetup* BodySetup = StaticMesh->GetBodySetup())
		{
			BodySetup->CreatePhysicsMeshes();
		}
	}

	return StaticMesh;
}

UStaticMesh* FFragMeshBuilder::BuildMergedStaticMesh(
	UObject* Outer,
	const TArray<FFragMergePart>& Parts,
	const FName& MeshName,
	const FLinearColor& Color,
	float Opacity,
	UMaterialInterface* Material,
	TArray<int32>* OutPartTriangleStarts,
	bool* OutTriangleOrderPreserved,
	bool bCookCollision)
{
	if (OutTriangleOrderPreserved)
	{
		*OutTriangleOrderPreserved = false;
	}

	if (Parts.IsEmpty())
	{
		return nullptr;
	}

	// Built before the UStaticMesh exists, so a chunk that emits nothing costs no
	// object at all. Creating one and discarding it is not equivalent: InitResources
	// hands the mesh to the render thread and the streaming manager, and tearing that
	// down by hand is how you get a null dereference on the render thread later.
	FMeshDescription MeshDesc;
	const int32 EmittedTriangles = PopulateMergedMeshDescription(Parts, MeshDesc, Color, Opacity, OutPartTriangleStarts);
	if (EmittedTriangles == 0)
	{
		return nullptr;
	}

	UStaticMesh* StaticMesh = NewObject<UStaticMesh>(Outer, MeshName, RF_Public | RF_Transactional | RF_Standalone);
	StaticMesh->InitResources();
	StaticMesh->SetLightingGuid(FGuid::NewGuid());

	// Only trust the baked normals if every part actually has them; one part
	// without normals would otherwise leave black, unlit triangles in the weld.
	bool bAllPartsHaveNormals = true;
	for (const FFragMergePart& Part : Parts)
	{
		if (Part.Geometry && Part.Geometry->Normals.Num() != Part.Geometry->Positions.Num())
		{
			bAllPartsHaveNormals = false;
			break;
		}
	}

	ConfigureSourceModel(StaticMesh, bAllPartsHaveNormals);

	TArray<const FMeshDescription*> MeshDescPtrs;
	MeshDescPtrs.Add(&MeshDesc);

	UStaticMesh::FBuildMeshDescriptionsParams BuildParams;
#if WITH_EDITOR
	BuildParams.bFastBuild = false;
#else
	BuildParams.bFastBuild = true;
#endif
	BuildParams.bBuildSimpleCollision = false;

	// Only the fast path takes the mesh description as it stands. The editor build
	// runs the vertex-cache optimiser, which permutes the triangles that the part
	// starts describe, and it does so again on every rebuild of a saved asset — so the
	// order cannot be relied on there even if this build happened to preserve it.
	if (OutTriangleOrderPreserved)
	{
		*OutTriangleOrderPreserved = BuildParams.bFastBuild;
	}

	StaticMesh->GetStaticMaterials().Add(MakeStaticMaterial(Material));

	if (bCookCollision)
	{
		if (!StaticMesh->GetBodySetup())
		{
			StaticMesh->CreateBodySetup();
		}
		if (UBodySetup* BodySetup = StaticMesh->GetBodySetup())
		{
			BodySetup->CollisionTraceFlag = CTF_UseComplexAsSimple;
			BodySetup->bDoubleSidedGeometry = true;
			BodySetup->bMeshCollideAll = true;
		}
	}

	StaticMesh->BuildFromMeshDescriptions(MeshDescPtrs, BuildParams);

	if (bCookCollision)
	{
		if (UBodySetup* BodySetup = StaticMesh->GetBodySetup())
		{
			BodySetup->CreatePhysicsMeshes();
		}
	}

	return StaticMesh;
}

int32 FFragMeshBuilder::PopulateMergedMeshDescription(
	const TArray<FFragMergePart>& Parts,
	FMeshDescription& OutMeshDesc,
	const FLinearColor& Color,
	float Opacity,
	TArray<int32>* OutPartTriangleStarts)
{
	FStaticMeshAttributes Attributes(OutMeshDesc);
	Attributes.Register();

	TVertexAttributesRef<FVector3f> VertexPositions = Attributes.GetVertexPositions();
	TVertexInstanceAttributesRef<FVector3f> VertexInstanceNormals = Attributes.GetVertexInstanceNormals();
	TVertexInstanceAttributesRef<FVector4f> VertexInstanceColors = Attributes.GetVertexInstanceColors();
	TVertexInstanceAttributesRef<FVector2f> VertexInstanceUVs = Attributes.GetVertexInstanceUVs();
	TPolygonGroupAttributesRef<FName> PolygonGroupImportedMaterialSlotNames = Attributes.GetPolygonGroupMaterialSlotNames();

	// Before any vertex instance exists, so nothing can be lost to a reallocation.
	// Note the UV written per instance below also stops two coincident corners from
	// different parts welding together during the build — which is required, not
	// incidental: a welded corner would carry one part's index and hand the other
	// part's triangles the wrong element.
	VertexInstanceUVs.SetNumChannels(1);

	if (OutPartTriangleStarts)
	{
		// The out-param is filled by appending, so a caller reusing an array would
		// otherwise silently desynchronise it from Parts.
		OutPartTriangleStarts->Reset();
	}

	if (Parts.Num() > GMaxEncodableParts)
	{
		UE_LOG(LogFragmentsUE, Error,
			TEXT("Merged mesh has %d parts, past the %d that UV0 can identify — face lookups past that point would name the wrong element."),
			Parts.Num(), GMaxEncodableParts);
	}

	FPolygonGroupID PolygonGroup = OutMeshDesc.CreatePolygonGroup();
	PolygonGroupImportedMaterialSlotNames[PolygonGroup] = FName("Material_0");

	// Summed in int64: as int32 these wrapped negative on a model that instanced one
	// index-heavy, vertex-poor shell a few hundred times, which silently turned the
	// reserves below into no-ops and then let the loop allocate until the process died.
	int64 TotalVertices = 0;
	int64 TotalIndices = 0;
	for (const FFragMergePart& Part : Parts)
	{
		if (Part.Geometry)
		{
			TotalVertices += Part.Geometry->Positions.Num();
			TotalIndices += Part.Geometry->Indices.Num();
		}
	}

	const int64 PlannedInstances = FMath::Min(TotalIndices, GMaxMergedVertexInstances);
	OutMeshDesc.ReserveNewVertices(static_cast<int32>(FMath::Min(TotalVertices, GMaxMergedDescriptionVertices)));
	OutMeshDesc.ReserveNewVertexInstances(static_cast<int32>(PlannedInstances));
	OutMeshDesc.ReserveNewPolygons(static_cast<int32>(PlannedInstances / 3));
	OutMeshDesc.ReserveNewEdges(static_cast<int32>(PlannedInstances));

	const FVector4f VertexColor(Color.R, Color.G, Color.B, Opacity);

	TArray<FVertexID> VertexIDs;
	int32 TriangleCount = 0;
	int64 EmittedInstances = 0;
	int64 EmittedVertices = 0;
	int32 DroppedParts = 0;

	for (int32 PartIndex = 0; PartIndex < Parts.Num(); ++PartIndex)
	{
		const FFragMergePart& Part = Parts[PartIndex];
		// Recorded even for empty parts, so the array stays parallel to Parts.
		// A face lookup takes the *last* part whose start is <= the face index,
		// which skips over any zero-triangle entries.
		if (OutPartTriangleStarts)
		{
			OutPartTriangleStarts->Add(TriangleCount);
		}

		if (!Part.Geometry)
		{
			continue;
		}

		const FFragGeometry& Geometry = *Part.Geometry;
		const int32 NumVertices = Geometry.Positions.Num();
		if (NumVertices == 0 || Geometry.Indices.Num() < 3)
		{
			continue;
		}

		// Charged per part rather than per triangle so a part is either whole or
		// absent; half of an element would render as torn geometry rather than as a
		// missing one. Its triangle start is still recorded above, as an empty range.
		//
		// Both counts are charged. Indices alone would leave a part with fifty million
		// positions and six indices free of charge, and the caller cannot catch that
		// either — it only ever flushes between parts, never inside one.
		if (EmittedInstances + Geometry.Indices.Num() > GMaxMergedVertexInstances
			|| EmittedVertices + NumVertices > GMaxMergedDescriptionVertices)
		{
			DroppedParts++;
			continue;
		}
		EmittedInstances += Geometry.Indices.Num();
		EmittedVertices += NumVertices;

		const FVector2f PartUV = EncodePartIndexAsUV(PartIndex);

		VertexIDs.Reset();
		VertexIDs.SetNum(NumVertices);
		for (int32 i = 0; i < NumVertices; ++i)
		{
			VertexIDs[i] = OutMeshDesc.CreateVertex();
			VertexPositions[VertexIDs[i]] = (FVector3f)Part.Transform.TransformPosition(Geometry.Positions[i]);
		}

		const bool bHasNormals = Geometry.Normals.Num() == NumVertices;
		const int32 NumTriangles = Geometry.Indices.Num() / 3;

		for (int32 i = 0; i < NumTriangles; ++i)
		{
			const int32 Idx0 = Geometry.Indices[i * 3 + 0];
			const int32 Idx1 = Geometry.Indices[i * 3 + 1];
			const int32 Idx2 = Geometry.Indices[i * 3 + 2];

			// The lower bound matters as much as the upper one: VertexIDs is indexed
			// directly below, so a negative index is an out-of-bounds read.
			if (Idx0 < 0 || Idx1 < 0 || Idx2 < 0
				|| Idx0 >= NumVertices || Idx1 >= NumVertices || Idx2 >= NumVertices)
			{
				continue;
			}

			const FVertexInstanceID Inst0 = OutMeshDesc.CreateVertexInstance(VertexIDs[Idx0]);
			const FVertexInstanceID Inst1 = OutMeshDesc.CreateVertexInstance(VertexIDs[Idx1]);
			const FVertexInstanceID Inst2 = OutMeshDesc.CreateVertexInstance(VertexIDs[Idx2]);

			TArray<FVertexInstanceID> TriangleInstances = { Inst0, Inst1, Inst2 };
			OutMeshDesc.CreatePolygon(PolygonGroup, TriangleInstances);

			if (bHasNormals)
			{
				VertexInstanceNormals[Inst0] = (FVector3f)Part.Transform.TransformVectorNoScale(Geometry.Normals[Idx0]).GetSafeNormal();
				VertexInstanceNormals[Inst1] = (FVector3f)Part.Transform.TransformVectorNoScale(Geometry.Normals[Idx1]).GetSafeNormal();
				VertexInstanceNormals[Inst2] = (FVector3f)Part.Transform.TransformVectorNoScale(Geometry.Normals[Idx2]).GetSafeNormal();
			}

			VertexInstanceColors[Inst0] = VertexColor;
			VertexInstanceColors[Inst1] = VertexColor;
			VertexInstanceColors[Inst2] = VertexColor;

			VertexInstanceUVs.Set(Inst0, 0, PartUV);
			VertexInstanceUVs.Set(Inst1, 0, PartUV);
			VertexInstanceUVs.Set(Inst2, 0, PartUV);

			TriangleCount++;
		}
	}

	if (DroppedParts > 0)
	{
		UE_LOG(LogFragmentsUE, Error,
			TEXT("Merged mesh reached its size ceiling; %d part(s) were left out of it."),
			DroppedParts);
	}

	return TriangleCount;
}

int32 FFragMeshBuilder::DecodePartIndexFromUV(const FVector2D& UV)
{
	// Range-checked before narrowing, not after. A zero-area triangle survives into
	// the cooked collision mesh (bRemoveDegenerates is off), and a barycentric solve
	// on one divides by zero — so a hit can hand back a NaN UV. Narrowing a NaN to
	// int32 yields 0 on MSVC, which is a perfectly valid part index: the lookup would
	// then report part 0's name and GUID for a hit on any degenerate triangle, with
	// nothing to show it had gone wrong. That is the silent-wrong-element failure this
	// encoding exists to remove.
	if (!FMath::IsFinite(UV.X) || !FMath::IsFinite(UV.Y)
		|| UV.X < -0.5 || UV.X > 1023.5
		|| UV.Y < -0.5 || UV.Y > 1023.5)
	{
		return INDEX_NONE;
	}

	const int32 Low = FMath::RoundToInt32(UV.X);
	const int32 High = FMath::RoundToInt32(UV.Y);
	return Low | (High << 10);
}

void FFragMeshBuilder::PopulateMeshDescription(const FFragGeometry& Geometry, FMeshDescription& OutMeshDesc, const FLinearColor& Color, float Opacity)
{
	FStaticMeshAttributes Attributes(OutMeshDesc);
	Attributes.Register();

	TVertexAttributesRef<FVector3f> VertexPositions = Attributes.GetVertexPositions();
	TVertexInstanceAttributesRef<FVector3f> VertexInstanceNormals = Attributes.GetVertexInstanceNormals();
	TVertexInstanceAttributesRef<FVector4f> VertexInstanceColors = Attributes.GetVertexInstanceColors();
	TPolygonGroupAttributesRef<FName> PolygonGroupImportedMaterialSlotNames = Attributes.GetPolygonGroupMaterialSlotNames();

	FPolygonGroupID PolygonGroup = OutMeshDesc.CreatePolygonGroup();
	PolygonGroupImportedMaterialSlotNames[PolygonGroup] = FName("Material_0");

	int32 NumVertices = Geometry.Positions.Num();
	int32 NumTriangles = Geometry.Indices.Num() / 3;

	OutMeshDesc.ReserveNewVertices(NumVertices);
	OutMeshDesc.ReserveNewVertexInstances(Geometry.Indices.Num());
	OutMeshDesc.ReserveNewPolygons(NumTriangles);
	OutMeshDesc.ReserveNewEdges(Geometry.Indices.Num());

	TArray<FVertexID> VertexIDs;
	VertexIDs.SetNum(NumVertices);
	for (int32 i = 0; i < NumVertices; ++i)
	{
		VertexIDs[i] = OutMeshDesc.CreateVertex();
		VertexPositions[VertexIDs[i]] = (FVector3f)Geometry.Positions[i];
	}

	bool bHasNormals = Geometry.Normals.Num() == NumVertices;

	for (int32 i = 0; i < NumTriangles; ++i)
	{
		int32 Idx0 = Geometry.Indices[i * 3 + 0];
		int32 Idx1 = Geometry.Indices[i * 3 + 1];
		int32 Idx2 = Geometry.Indices[i * 3 + 2];

		// Ensure valid indices — a negative one indexes VertexIDs out of bounds just
		// as surely as an over-large one.
		if (Idx0 < 0 || Idx1 < 0 || Idx2 < 0
			|| Idx0 >= NumVertices || Idx1 >= NumVertices || Idx2 >= NumVertices)
		{
			continue;
		}

		FVertexInstanceID Inst0 = OutMeshDesc.CreateVertexInstance(VertexIDs[Idx0]);
		FVertexInstanceID Inst1 = OutMeshDesc.CreateVertexInstance(VertexIDs[Idx1]);
		FVertexInstanceID Inst2 = OutMeshDesc.CreateVertexInstance(VertexIDs[Idx2]);

		TArray<FVertexInstanceID> TriangleInstances = { Inst0, Inst1, Inst2 };
		OutMeshDesc.CreatePolygon(PolygonGroup, TriangleInstances);

		if (bHasNormals)
		{
			VertexInstanceNormals[Inst0] = (FVector3f)Geometry.Normals[Idx0];
			VertexInstanceNormals[Inst1] = (FVector3f)Geometry.Normals[Idx1];
			VertexInstanceNormals[Inst2] = (FVector3f)Geometry.Normals[Idx2];
		}
		
		VertexInstanceColors[Inst0] = FVector4f(Color.R, Color.G, Color.B, Opacity);
		VertexInstanceColors[Inst1] = FVector4f(Color.R, Color.G, Color.B, Opacity);
		VertexInstanceColors[Inst2] = FVector4f(Color.R, Color.G, Color.B, Opacity);
		// If not bHasNormals, Unreal will recompute them because we'll set bRecomputeNormals dynamically.
	}
}

void FFragMeshBuilder::BuildProceduralMesh(const FFragGeometry& Geometry, UProceduralMeshComponent* ProcMeshComp, const FLinearColor& Color, float Opacity, bool bCreateCollision)
{
	if (!ProcMeshComp || Geometry.Positions.IsEmpty() || Geometry.Indices.IsEmpty())
	{
		return;
	}

	TArray<FVector> Vertices = Geometry.Positions;
	TArray<int32> Triangles = Geometry.Indices;
	TArray<FVector> Normals = Geometry.Normals;
	TArray<FVector2D> UV0; // Empty for now
	TArray<FLinearColor> VertexColors;
	VertexColors.Init(FLinearColor(Color.R, Color.G, Color.B, Opacity), Vertices.Num());
	TArray<FProcMeshTangent> Tangents;

	ProcMeshComp->CreateMeshSection_LinearColor(
		0, 
		Vertices, 
		Triangles, 
		Normals, 
		UV0, 
		VertexColors,
		Tangents,
		bCreateCollision
	);
}

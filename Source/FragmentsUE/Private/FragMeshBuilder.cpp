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

	static constexpr int64 GMaxMergedVertexInstances = 8 * 1000 * 1000;

	static constexpr int64 GMaxMergedDescriptionVertices = 4 * 1000 * 1000;

	static constexpr int32 GMaxEncodableParts = 1 << 20;

	/** Two 10-bit halves: half-precision UVs are exact only to 2048, so each half stays <1024. */
	FVector2f EncodePartIndexAsUV(int32 PartIndex)
	{
		return FVector2f(
			static_cast<float>(PartIndex & 1023),
			static_cast<float>((PartIndex >> 10) & 1023));
	}

	/** Imported-slot-name is editor-only; the 3-arg constructor does not compile when packaging. */
	FStaticMaterial MakeStaticMaterial(UMaterialInterface* Material)
	{
#if WITH_EDITORONLY_DATA
		return FStaticMaterial(Material, GMaterialSlotName, GMaterialSlotName);
#else
		return FStaticMaterial(Material, GMaterialSlotName);
#endif
	}

	/** Source models are editor-only; packaged builds take the mesh description as-is. */
	void ConfigureSourceModel(UStaticMesh* StaticMesh, bool bHasNormals)
	{
#if WITH_EDITOR
		FStaticMeshSourceModel& SourceModel = StaticMesh->AddSourceModel();
		SourceModel.BuildSettings.bRecomputeNormals = !bHasNormals;
		SourceModel.BuildSettings.bRecomputeTangents = true;
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

	// Build first: InitResources hands the mesh to the render thread and undoing that risks a crash.
	FMeshDescription MeshDesc;
	const int32 EmittedTriangles = PopulateMergedMeshDescription(Parts, MeshDesc, Color, Opacity, OutPartTriangleStarts);
	if (EmittedTriangles == 0)
	{
		return nullptr;
	}

	UStaticMesh* StaticMesh = NewObject<UStaticMesh>(Outer, MeshName, RF_Public | RF_Transactional | RF_Standalone);
	StaticMesh->InitResources();
	StaticMesh->SetLightingGuid(FGuid::NewGuid());

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

	// Editor builds run the vertex-cache optimiser, permuting triangle order; fast builds do not.
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

	VertexInstanceUVs.SetNumChannels(1);

	if (OutPartTriangleStarts)
	{
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

	// Summed in int64: as int32 these wrapped negative and silently turned the reserves into no-ops.
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

			// Negative indices matter too: VertexIDs is indexed directly below, reading out of bounds.
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
	// NaN UVs from degenerate-triangle barycentrics narrow to 0 on MSVC: a valid but wrong index.
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

		// Negative indices matter too: VertexIDs is indexed directly below, reading out of bounds.
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
	TArray<FVector2D> UV0;
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

#include "FragMeshBuilder.h"
#include "Engine/StaticMesh.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
#include "ProceduralMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "UObject/Package.h"
#include "PhysicsEngine/BodySetup.h"

UStaticMesh* FFragMeshBuilder::BuildStaticMesh(const FFragGeometry& Geometry, UObject* Outer, const FName& MeshName)
{
	if (Geometry.Positions.IsEmpty() || Geometry.Indices.IsEmpty())
	{
		return nullptr;
	}

	UStaticMesh* StaticMesh = NewObject<UStaticMesh>(Outer, MeshName, RF_Public | RF_Transient | RF_Standalone);
	StaticMesh->InitResources();

	StaticMesh->SetLightingGuid(FGuid::NewGuid());

	// Set up source model
	FStaticMeshSourceModel& SourceModel = StaticMesh->AddSourceModel();
	bool bHasNormals = Geometry.Normals.Num() > 0 && Geometry.Normals.Num() == Geometry.Positions.Num();
	SourceModel.BuildSettings.bRecomputeNormals = !bHasNormals; // Recompute only if missing
	SourceModel.BuildSettings.bRecomputeTangents = true; // MUST be true for PBR materials!
	SourceModel.BuildSettings.bUseMikkTSpace = false; // Disable MikkTSpace to prevent CAD shading corruption
	SourceModel.BuildSettings.bRemoveDegenerates = false; // Prevent removing back-face triangles
	SourceModel.BuildSettings.bUseHighPrecisionTangentBasis = false;
	SourceModel.BuildSettings.bUseFullPrecisionUVs = false;
	SourceModel.BuildSettings.bGenerateLightmapUVs = false;
	SourceModel.BuildSettings.SrcLightmapIndex = 0;
	SourceModel.BuildSettings.DstLightmapIndex = 1;

	FMeshDescription MeshDesc;
	PopulateMeshDescription(Geometry, MeshDesc);

	TArray<const FMeshDescription*> MeshDescPtrs;
	MeshDescPtrs.Add(&MeshDesc);

	UStaticMesh::FBuildMeshDescriptionsParams BuildParams;
#if WITH_EDITOR
	BuildParams.bFastBuild = false; // Normal build generates collision properly in Editor
#else
	BuildParams.bFastBuild = true; // Fast path required at runtime in packaged builds
#endif
	BuildParams.bBuildSimpleCollision = false;

	StaticMesh->GetStaticMaterials().Add(FStaticMaterial(nullptr, FName("Material_0"), FName("Material_0")));
	StaticMesh->BuildFromMeshDescriptions(MeshDescPtrs, BuildParams);

	// Ensure collision settings are correct
	if (UBodySetup* BodySetup = StaticMesh->GetBodySetup())
	{
		BodySetup->CollisionTraceFlag = CTF_UseComplexAsSimple;
	}

	return StaticMesh;
}

void FFragMeshBuilder::PopulateMeshDescription(const FFragGeometry& Geometry, FMeshDescription& OutMeshDesc)
{
	FStaticMeshAttributes Attributes(OutMeshDesc);
	Attributes.Register();

	TVertexAttributesRef<FVector3f> VertexPositions = Attributes.GetVertexPositions();
	TVertexInstanceAttributesRef<FVector3f> VertexInstanceNormals = Attributes.GetVertexInstanceNormals();
	TVertexInstanceAttributesRef<FVector2f> VertexInstanceUVs = Attributes.GetVertexInstanceUVs();
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

		// Ensure valid indices
		if (Idx0 >= NumVertices || Idx1 >= NumVertices || Idx2 >= NumVertices)
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
		// If not bHasNormals, Unreal will recompute them because we'll set bRecomputeNormals dynamically.
		auto GetPlanarUV = [](const FVector3f& Pos, const FVector3f& Normal) -> FVector2f
		{
			FVector3f AbsNormal(FMath::Abs(Normal.X), FMath::Abs(Normal.Y), FMath::Abs(Normal.Z));
			if (AbsNormal.Z >= AbsNormal.X && AbsNormal.Z >= AbsNormal.Y)
				return FVector2f(Pos.X, Pos.Y) * 0.01f;
			else if (AbsNormal.X >= AbsNormal.Y)
				return FVector2f(Pos.Y, Pos.Z) * 0.01f;
			else
				return FVector2f(Pos.X, Pos.Z) * 0.01f;
		};

		FVector3f P0 = (FVector3f)Geometry.Positions[Idx0];
		FVector3f P1 = (FVector3f)Geometry.Positions[Idx1];
		FVector3f P2 = (FVector3f)Geometry.Positions[Idx2];

		FVector3f FaceNormal = FVector3f::CrossProduct(P1 - P0, P2 - P0).GetSafeNormal();
		if (FaceNormal.IsNearlyZero())
		{
			FaceNormal = FVector3f(0, 0, 1);
		}

		VertexInstanceUVs[Inst0] = GetPlanarUV(P0, FaceNormal);
		VertexInstanceUVs[Inst1] = GetPlanarUV(P1, FaceNormal);
		VertexInstanceUVs[Inst2] = GetPlanarUV(P2, FaceNormal);

		VertexInstanceColors[Inst0] = FVector4f(1.0f, 1.0f, 1.0f, 1.0f);
		VertexInstanceColors[Inst1] = FVector4f(1.0f, 1.0f, 1.0f, 1.0f);
		VertexInstanceColors[Inst2] = FVector4f(1.0f, 1.0f, 1.0f, 1.0f);
	}
}

void FFragMeshBuilder::BuildProceduralMesh(const FFragGeometry& Geometry, UProceduralMeshComponent* ProcMeshComp)
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
	TArray<FProcMeshTangent> Tangents;

	VertexColors.Init(FLinearColor::White, Vertices.Num());

	ProcMeshComp->CreateMeshSection_LinearColor(
		0, 
		Vertices, 
		Triangles, 
		Normals, 
		UV0, 
		VertexColors, 
		Tangents, 
		false // bCreateCollision
	);
}

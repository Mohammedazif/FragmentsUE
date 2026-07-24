#include "FragMeshBuilder.h"
#include "Engine/StaticMesh.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
#include "ProceduralMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "UObject/Package.h"
#include "PhysicsEngine/BodySetup.h"

UStaticMesh* FFragMeshBuilder::BuildStaticMesh(UObject* Outer, const FFragGeometry& Geometry, const FName& MeshName, const FLinearColor& Color, float Opacity, UMaterialInterface* Material)
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
	SourceModel.BuildSettings.bRecomputeNormals = !bHasNormals; // Recompute only if our normals are missing
	SourceModel.BuildSettings.bRecomputeTangents = true;        // Always recompute tangents from geometry
	SourceModel.BuildSettings.bUseMikkTSpace = false;           // Disable MikkTSpace: our planar UVs (0.01 scale) create degenerate tangent bases with MikkTSpace
	SourceModel.BuildSettings.bRemoveDegenerates = false;       // Keep all faces: IFC thin geometry would be removed otherwise
	SourceModel.BuildSettings.bUseHighPrecisionTangentBasis = false;
	SourceModel.BuildSettings.bUseFullPrecisionUVs = false;
	SourceModel.BuildSettings.bGenerateLightmapUVs = false;
	SourceModel.BuildSettings.SrcLightmapIndex = 0;
	SourceModel.BuildSettings.DstLightmapIndex = 1;

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

	if (Material)
	{
		StaticMesh->GetStaticMaterials().Add(FStaticMaterial(Material, FName("Material_0"), FName("Material_0")));
	}
	else
	{
		StaticMesh->GetStaticMaterials().Add(FStaticMaterial(nullptr, FName("Material_0"), FName("Material_0")));
	}
	
	// Ensure collision settings are correct before building the mesh
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

	StaticMesh->BuildFromMeshDescriptions(MeshDescPtrs, BuildParams);
	
	// Force cook the complex physics mesh for runtime use
	if (UBodySetup* BodySetup = StaticMesh->GetBodySetup())
	{
		BodySetup->CreatePhysicsMeshes();
	}

	return StaticMesh;
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
		
		VertexInstanceColors[Inst0] = FVector4f(Color.R, Color.G, Color.B, Opacity);
		VertexInstanceColors[Inst1] = FVector4f(Color.R, Color.G, Color.B, Opacity);
		VertexInstanceColors[Inst2] = FVector4f(Color.R, Color.G, Color.B, Opacity);
		// If not bHasNormals, Unreal will recompute them because we'll set bRecomputeNormals dynamically.
	}
}

void FFragMeshBuilder::BuildProceduralMesh(const FFragGeometry& Geometry, UProceduralMeshComponent* ProcMeshComp, const FLinearColor& Color, float Opacity)
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
		true // bCreateCollision
	);
}

#include "FragmentsActor.h"
#include "FragMeshBuilder.h"
#include "FragAssetFactory.h"
#include "FragmentsUEModule.h"
#include "FragmentsElementActor.h"
#include "FragmentsMetadataComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "ProceduralMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "Engine/HitResult.h"
#include "Kismet/GameplayStatics.h"
#include "PhysicsEngine/PhysicsSettings.h"
#include "UObject/Package.h"
#include "RenderingThread.h"
#include "Misc/ScopedSlowTask.h"

AFragmentsActor::AFragmentsActor()
{
	PrimaryActorTick.bCanEverTick = false;

	ModelRoot = CreateDefaultSubobject<USceneComponent>(TEXT("ModelRoot"));
	ModelRoot->SetMobility(EComponentMobility::Static);
	RootComponent = ModelRoot;

	ModelMetadataComponent = CreateDefaultSubobject<UFragmentsMetadataComponent>(TEXT("IFCModelInfo"));
}

void AFragmentsActor::Destroyed()
{
	Super::Destroyed();
	for (AActor* Child : SpawnedChildActors)
	{
		if (IsValid(Child))
		{
			Child->Destroy();
		}
	}
	SpawnedChildActors.Empty();
	LocalIdToActors.Empty();
	HiddenLocalIds.Empty();
}

UMaterialInterface* AFragmentsActor::GetOrCreateMaterial(
	UMaterialInterface* BaseMaterial, 
	const FLinearColor& Color, 
	float Opacity, 
	bool bDoubleSided,
	TMap<uint32, UMaterialInterface*>& OutMaterialCache,
	bool bIsGlass)
{
	if (!BaseMaterial) return nullptr;

	uint32 ColorHash = GetTypeHash(Color) ^ GetTypeHash(Opacity) ^ (bIsGlass ? 1 : 0);
	
	if (UMaterialInterface** FoundMID = OutMaterialCache.Find(ColorHash))
	{
		return *FoundMID;
	}

	// Baking writes a real Material Instance asset so the level can be packaged.
	// A dynamic instance is the fallback, and the only option at runtime.
	UMaterialInterface* Result = nullptr;

	if (AssetFactory.IsValid())
	{
		const FString AssetName = FString::Printf(TEXT("MI_%s_%08X"), *ModelName, ColorHash);
		Result = AssetFactory->CreateMaterialInstance(BaseMaterial, AssetName, Color, Opacity, bIsGlass);
	}

	if (!Result)
	{
		UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(BaseMaterial, this);
		MID->SetVectorParameterValue(TEXT("BaseColor"), Color); // M_FragBase parameter
		MID->SetVectorParameterValue(TEXT("Color"), Color); // Datasmith materials use "Color"
		MID->SetScalarParameterValue(TEXT("Opacity"), Opacity); // M_FragBase opacity parameter

		if (!bIsGlass)
		{
			MID->SetScalarParameterValue(TEXT("Roughness"), 0.65f);
			MID->SetScalarParameterValue(TEXT("Specular"), 0.4f);
		}

		Result = MID;
	}

	OutMaterialCache.Add(ColorHash, Result);
	return Result;
}

static void ApplyPickingCollision(UPrimitiveComponent* Component, bool bEnablePicking, ECollisionChannel PickingChannel)
{
	if (!Component)
	{
		return;
	}

	if (!bEnablePicking)
	{
		Component->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
		Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Component->SetGenerateOverlapEvents(false);
		return;
	}

	Component->SetCollisionProfileName(UCollisionProfile::CustomCollisionProfileName);
	Component->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Component->SetCollisionObjectType(ECC_WorldStatic);
	Component->SetCollisionResponseToAllChannels(ECR_Ignore);
	Component->SetCollisionResponseToChannel(PickingChannel, ECR_Block);

	// Movement sweeps run on these. Re-ignored after the picking channel in case it is
	// one of them, so a trace channel that doubles as a movement channel still cannot
	// block the player — picking is worth less than being able to walk.
	Component->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	Component->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Ignore);
	Component->SetCollisionResponseToChannel(ECC_Vehicle, ECR_Ignore);
	Component->SetCollisionResponseToChannel(ECC_Destructible, ECR_Ignore);

	Component->SetGenerateOverlapEvents(false);
	Component->CanCharacterStepUpOn = ECB_No;
	Component->bFillCollisionUnderneathForNavmesh = false;
	Component->SetCanEverAffectNavigation(false);
}

UStaticMesh* AFragmentsActor::CreateStaticMesh(
	const FFragGeometry& Geometry,
	const FString& BaseName,
	const FLinearColor& Color,
	float Opacity,
	UMaterialInterface* Material)
{
	UObject* Outer = this;
	FString ObjectName = BaseName;

	if (AssetFactory.IsValid())
	{
		FString UniqueName;
		if (UPackage* Package = AssetFactory->CreateAssetPackage(TEXT("Geometry"), BaseName, UniqueName))
		{
			Outer = Package;
			ObjectName = UniqueName;
		}
	}

	UStaticMesh* Mesh = FFragMeshBuilder::BuildStaticMesh(Outer, Geometry, *ObjectName, Color, Opacity, Material, bPickingEnabled);

	if (Mesh && Outer != this)
	{
		AssetFactory->RegisterAsset(Mesh);
	}

	return Mesh;
}

UStaticMesh* AFragmentsActor::CreateMergedStaticMesh(
	const TArray<FFragMergePart>& Parts,
	const FString& BaseName,
	const FLinearColor& Color,
	float Opacity,
	UMaterialInterface* Material,
	TArray<int32>* OutTriangleStarts,
	bool* OutTriangleOrderPreserved,
	UObject* FallbackOuter)
{
	UObject* Outer = FallbackOuter;
	FString ObjectName = BaseName;

	if (AssetFactory.IsValid())
	{
		FString UniqueName;
		if (UPackage* Package = AssetFactory->CreateAssetPackage(TEXT("Geometry"), BaseName, UniqueName))
		{
			Outer = Package;
			ObjectName = UniqueName;
		}
	}

	UStaticMesh* Mesh = FFragMeshBuilder::BuildMergedStaticMesh(
		Outer, Parts, *ObjectName, Color, Opacity, Material, OutTriangleStarts, OutTriangleOrderPreserved, bPickingEnabled);

	if (Mesh && Outer != FallbackOuter)
	{
		AssetFactory->RegisterAsset(Mesh);
	}

	return Mesh;
}

void AFragmentsActor::FinishAssetCreation()
{
	if (!AssetFactory.IsValid())
	{
		return;
	}

	if (AssetFactory->GetAssetCount() > 0)
	{
		AssetFactory->SaveAll();
	}

	AssetFactory.Reset();
}

void AFragmentsActor::SetElementPickingEnabled(bool bEnabled, TEnumAsByte<ECollisionChannel> TraceChannel)
{
	bPickingEnabled = bEnabled;
	PickingChannel = TraceChannel;

	TArray<UPrimitiveComponent*> Components;
	GetComponents<UPrimitiveComponent>(Components);
	for (UPrimitiveComponent* Component : Components)
	{
		ApplyPickingCollision(Component, bPickingEnabled, PickingChannel);
	}

	for (AActor* Child : SpawnedChildActors)
	{
		if (!IsValid(Child))
		{
			continue;
		}

		Child->SetActorEnableCollision(bPickingEnabled);

		TArray<UPrimitiveComponent*> ChildComponents;
		Child->GetComponents<UPrimitiveComponent>(ChildComponents);
		for (UPrimitiveComponent* Component : ChildComponents)
		{
			ApplyPickingCollision(Component, bPickingEnabled, PickingChannel);
		}
	}
}

void AFragmentsActor::DiscardCancelledBuild()
{
	bImportWasCancelled = true;

	// Saving here would leave a half-populated asset folder on disk and log that the
	// level is ready to package.
	AssetFactory.Reset();

	for (AActor* Spawned : SpawnedChildActors)
	{
		if (IsValid(Spawned))
		{
			Spawned->Destroy();
		}
	}
	SpawnedChildActors.Reset();
	LocalIdToActors.Reset();
	HiddenLocalIds.Reset();

	ItemMetadata.Reset();
	LocalIdToMetadataIndex.Reset();
	GuidToMetadataIndex.Reset();
	InstanceMetadata.Reset();
	MergedElementMetadata.Reset();

	// Cleared so the same actor can be built again rather than sitting half-imported.
	bHasBuiltHierarchy = false;

	UE_LOG(LogFragmentsUE, Warning, TEXT("Import cancelled — nothing was written and the actor is empty."));
}

void AFragmentsActor::BuildFromImportResult(
	const FFragImportResult& Result, 
	const FFragImportOptions& Options, 
	UMaterialInterface* BaseMaterial,
	UMaterialInterface* TranslucentMaterial,
	UMaterialInterface* GlassMaterial)
{
	if (bHasBuiltHierarchy)
	{
		return;
	}
	bHasBuiltHierarchy = true;

	if (!Result.bSuccess)
	{
		bHasBuiltHierarchy = false;
		return;
	}

	bPickingEnabled = Options.bEnableElementPicking;
	PickingChannel = Options.PickingTraceChannel;

	// Kept for the filtering API, which can only offer what the chosen mode built.
	BuiltImportMode = Options.ImportMode;
	LocalIdToActors.Reset();
	HiddenLocalIds.Reset();

#if WITH_EDITOR
	// Merged meshes identify their element through UV0, because the editor build
	// reorders triangles. Reading it needs this project setting, and without it the
	// lookup returns nothing rather than something wrong.
	if (bPickingEnabled && FragModeIsMerged(Options.ImportMode)
		&& !UPhysicsSettings::Get()->bSupportUVFromHitResults)
	{
		UE_LOG(LogFragmentsUE, Warning,
			TEXT("Element picking on merged meshes needs Project Settings > Physics > Support UV From Hit Results. ")
			TEXT("It is off, so GetMetadataFromHit will not resolve merged elements."));
	}
#endif
	bCancelRequested = false;
	bImportWasCancelled = false;
	bReportedActorLimit = false;

	ModelGuid = Result.ModelGuid;
	ModelName = Result.ModelName;
	ModelMetadataJson = Result.Metadata;
	if (ModelMetadataComponent)
	{
		ModelMetadataComponent->ItemData = Result.ModelInfo;
	}
	BuildMetadataTables(Result);

#if WITH_EDITOR
	// Second stage of the import: everything generated below goes into the
	// Content Browser instead of living on this actor, so the level can be saved
	// and cooked. Outside the editor this stays null and the build is transient.
	if (Options.bSaveAsAssets)
	{
		AssetFactory = MakeShared<FFragAssetFactory>(Options.AssetPath, Result.ModelName);
		if (!AssetFactory->IsValid())
		{
			AssetFactory.Reset();
		}
	}
#endif

	TMap<int64, UStaticMesh*> StaticMeshCache;
	TMap<uint32, UMaterialInterface*> MaterialCache;

	// Whole-model merge has no hierarchy left to build, so it short-circuits.
	// The scoped modes run inside the hierarchy walk instead.
	if (Options.ImportMode == EFragImportMode::MergedWholeModel)
	{
		TArray<const FFragInstance*> AllInstances;
		AllInstances.Reserve(Result.Instances.Num());
		for (const FFragInstance& Instance : Result.Instances)
		{
			AllInstances.Add(&Instance);
		}

		const int32 Built = BuildMergedComponents(
			AllInstances, Result, this, TEXT("Model"),
			BaseMaterial, TranslucentMaterial, GlassMaterial, MaterialCache);

		FinishAssetCreation();
		return;
	}

	if (FragModeUsesHierarchy(Options.ImportMode) && Result.SpatialRoot.Children.Num() > 0)
	{
		TMap<int32, TArray<const FFragInstance*>> InstancesByLocalId;
		for (const FFragInstance& Instance : Result.Instances)
		{
			InstancesByLocalId.FindOrAdd(Instance.LocalId).Add(&Instance);
		}
		int32 SpawnCount = 0;

		// One set for the whole walk, not one per node: an id repeated under a
		// different parent is the same geometry in the same place, and realising it
		// twice is duplicated work for an identical picture.
		TSet<int32> ConsumedLocalIds;
		bReportedActorLimit = false;

		FScopedSlowTask* SlowTaskPtr = nullptr;
#if WITH_EDITOR
		FScopedSlowTask SlowTask(Result.Instances.Num(), FText::FromString("Building Fragments Hierarchy..."));
		SlowTask.MakeDialog(/*bShowCancelButton*/ true);
		SlowTaskPtr = &SlowTask;
#endif
		SpawnHierarchyNode(Result.SpatialRoot, this, InstancesByLocalId, ConsumedLocalIds, StaticMeshCache, MaterialCache, Result, Options, BaseMaterial, TranslucentMaterial, GlassMaterial, SpawnCount, SlowTaskPtr);

		if (bCancelRequested)
		{
			DiscardCancelledBuild();
			return;
		}

		FinishAssetCreation();
		return;
	}


	// 1. Find all unique Geometry+Material pairs from the instances
	struct FGeomMatPair
	{
		int32 GeometryIndex;
		int32 MaterialIndex;
		FLinearColor Color;
		float Opacity;
		bool bDoubleSided;
		bool bIsWindowCategory;
	};

	TMap<int64, FGeomMatPair> UniquePairs;
	for (const FFragInstance& Instance : Result.Instances)
	{
		int64 Key = (static_cast<int64>(Instance.GeometryIndex) << 32) | static_cast<uint32>(Instance.MaterialIndex);
		if (!UniquePairs.Contains(Key))
		{
			FGeomMatPair Pair;
			Pair.GeometryIndex = Instance.GeometryIndex;
			Pair.MaterialIndex = Instance.MaterialIndex;
			Pair.Color = Instance.Color;
			Pair.Opacity = Instance.Opacity;
			Pair.bDoubleSided = Instance.bDoubleSided;
			Pair.bIsWindowCategory = Instance.Category.Contains(TEXT("IfcWindow"), ESearchCase::IgnoreCase) || Instance.Category.Contains(TEXT("IfcPlate"), ESearchCase::IgnoreCase);
			UniquePairs.Add(Key, Pair);
		}
		else
		{
			if (Instance.Category.Contains(TEXT("IfcWindow"), ESearchCase::IgnoreCase) || Instance.Category.Contains(TEXT("IfcPlate"), ESearchCase::IgnoreCase))
			{
				UniquePairs[Key].bIsWindowCategory = true;
			}
		}
	}

	// 2. Create meshes and components for each unique pair
	for (const auto& PairKV : UniquePairs)
	{
		int64 Key = PairKV.Key;
		const FGeomMatPair& Pair = PairKV.Value;
		if (Pair.GeometryIndex < 0 || Pair.GeometryIndex >= Result.Geometries.Num())
			continue;
			
		const FFragGeometry& Geom = Result.Geometries[Pair.GeometryIndex];

		UMaterialInterface* TargetMaterial = BaseMaterial;
		bool bIsGlass = false;
		float AdjustedOpacity = Pair.Opacity;

		bool bIsBlueColor = (Pair.Color.B > Pair.Color.R + 0.15f && Pair.Color.B > Pair.Color.G + 0.05f);

		if (Pair.Opacity < 0.99f || bIsBlueColor)
		{
			if ((bIsBlueColor || Pair.Opacity < 0.99f) && GlassMaterial)
			{
				TargetMaterial = GlassMaterial;
				bIsGlass = true;
				if (AdjustedOpacity >= 0.99f) AdjustedOpacity = 0.75f;
			}
			else if (TranslucentMaterial)
			{
				TargetMaterial = TranslucentMaterial;
			}
		}

		FLinearColor CorrectedColor;
		CorrectedColor.R = FMath::Pow(Pair.Color.R, 2.2f);
		CorrectedColor.G = FMath::Pow(Pair.Color.G, 2.2f);
		CorrectedColor.B = FMath::Pow(Pair.Color.B, 2.2f);
		CorrectedColor.A = Pair.Color.A;

		UMaterialInterface* TargetMID = GetOrCreateMaterial(TargetMaterial, CorrectedColor, AdjustedOpacity, Pair.bDoubleSided, MaterialCache, bIsGlass);
		
		if (Options.ImportMode != EFragImportMode::Procedural)
		{
			UStaticMesh* StaticMesh = nullptr;
			if (UStaticMesh** CachedMesh = StaticMeshCache.Find(Key))
			{
				StaticMesh = *CachedMesh;
			}
			else
			{
				FString MeshName = FString::Printf(TEXT("SM_FragGeom_%d_Mat_%d"), Geom.GeometryIndex, Pair.MaterialIndex);
				StaticMesh = CreateStaticMesh(Geom, MeshName, CorrectedColor, AdjustedOpacity, TargetMID);
				if (StaticMesh)
				{
					StaticMeshCache.Add(Key, StaticMesh);
				}
			}

			if (StaticMesh)
			{
				FString CompName = FString::Printf(TEXT("ISMC_%d_Mat_%d"), Pair.GeometryIndex, Pair.MaterialIndex);
				UInstancedStaticMeshComponent* ISMC = NewObject<UInstancedStaticMeshComponent>(this, *CompName);
				ISMC->SetStaticMesh(StaticMesh);
				ISMC->SetupAttachment(ModelRoot);
				
				ISMC->SetMaterial(0, TargetMID);
				ApplyPickingCollision(ISMC, bPickingEnabled, PickingChannel);
				
				if (bIsGlass)
				{
					UE_LOG(LogFragmentsUE, Warning, TEXT("WINDOW DEBUG: Applied Glass Material to component %s"), *ISMC->GetName());
				}

				ISMC->RegisterComponent();
				InstancedMeshes.Add(Key, ISMC);
			}
		}
		else
		{
			FString CompName = FString::Printf(TEXT("PMC_%d_Mat_%d"), Pair.GeometryIndex, Pair.MaterialIndex);
			UProceduralMeshComponent* PMC = NewObject<UProceduralMeshComponent>(this, *CompName);
			PMC->SetupAttachment(ModelRoot);
			PMC->bUseAsyncCooking = true;

			FFragMeshBuilder::BuildProceduralMesh(Geom, PMC, CorrectedColor, AdjustedOpacity, bPickingEnabled);
			PMC->SetMaterial(0, TargetMID);
			ApplyPickingCollision(PMC, bPickingEnabled, PickingChannel);

			PMC->RegisterComponent();
			ProceduralMeshes.Add(Key, PMC);
		}
	}

	// 3. Add instances
	//
	// Procedural mode has no instancing to fall back on: UProceduralMeshComponent keeps
	// its section arrays on the CPU for the component's lifetime, so every placement
	// after the first is a deep copy of all of them plus its own GPU buffers. Cost grows
	// with instances x vertices where the instanced path grows with instances + vertices,
	// and a file pairing one large geometry with a long sample list turns a few megabytes
	// on disk into an arbitrary allocation. Bound the total and name the mode that does
	// this properly. Only duplicates are charged; the templates were built above.
	static constexpr int64 GMaxProceduralDuplicateVertices = 8 * 1000 * 1000;
	int64 ProceduralDuplicateVertices = 0;
	bool bReportedProceduralLimit = false;

	for (const FFragInstance& Instance : Result.Instances)
	{
		int64 Key = (static_cast<int64>(Instance.GeometryIndex) << 32) | static_cast<uint32>(Instance.MaterialIndex);
		if (Options.ImportMode != EFragImportMode::Procedural)
		{
			if (UInstancedStaticMeshComponent** ISMC_Ptr = InstancedMeshes.Find(Key))
			{
				const int32 InstanceIndex = (*ISMC_Ptr)->AddInstance(Instance.Transform, true);

				// Remember which element each instance came from so a trace hit
				// on this component can be resolved back to its IFC metadata.
				if (InstanceIndex != INDEX_NONE)
				{
					const int32* MetadataIndex = LocalIdToMetadataIndex.Find(Instance.LocalId);
					FFragInstanceMetadataMap& Map = InstanceMetadata.FindOrAdd(*ISMC_Ptr);
					while (Map.MetadataIndices.Num() <= InstanceIndex)
					{
						Map.MetadataIndices.Add(INDEX_NONE);
					}
					Map.MetadataIndices[InstanceIndex] = MetadataIndex ? *MetadataIndex : INDEX_NONE;
				}
			}
		}
		else
		{
			if (UProceduralMeshComponent** PMC_Ptr = ProceduralMeshes.Find(Key))
			{
				UProceduralMeshComponent* TemplatePMC = *PMC_Ptr;
				
				if (TemplatePMC->GetRelativeTransform().Equals(FTransform::Identity))
				{
					TemplatePMC->SetWorldTransform(Instance.Transform);
				}
				else
				{
					const int32 GeometryVertices = Result.Geometries.IsValidIndex(Instance.GeometryIndex)
						? Result.Geometries[Instance.GeometryIndex].Positions.Num()
						: 0;

					if (ProceduralDuplicateVertices + GeometryVertices > GMaxProceduralDuplicateVertices)
					{
						if (!bReportedProceduralLimit)
						{
							bReportedProceduralLimit = true;
							UE_LOG(LogFragmentsUE, Error,
								TEXT("Procedural mode stopped after %lld duplicated vertices; the remaining placements were skipped. ")
								TEXT("Procedural components cannot share geometry — re-import in Instanced or a merged mode for a model this size."),
								ProceduralDuplicateVertices);
						}
						continue;
					}
					ProceduralDuplicateVertices += GeometryVertices;

					FString CompName = FString::Printf(TEXT("PMC_%d_Mat_%d_Inst_%d"), Instance.GeometryIndex, Instance.MaterialIndex, Instance.LocalId);
					UProceduralMeshComponent* NewPMC = DuplicateObject<UProceduralMeshComponent>(TemplatePMC, this, *CompName);
					NewPMC->SetupAttachment(ModelRoot);
					NewPMC->SetWorldTransform(Instance.Transform);
					NewPMC->RegisterComponent();
				}
			}
		}
	}

	FinishAssetCreation();
}



// ─────────────────────────────────────────────────────────────────────────────
// Merge by Category
// ─────────────────────────────────────────────────────────────────────────────

/** Cap on one merged mesh, so a large category splits instead of building a monster. */
static constexpr int32 GMaxMergedVertices = 500000;

/**
 * The same cap counted in indices. Typical BIM geometry runs about three indices per
 * vertex, so this is roughly the same size of chunk by a second measure — one that a
 * vertex-poor, index-heavy shell cannot slip past.
 */
static constexpr int64 GMaxMergedIndices = 4000000;

int32 AFragmentsActor::BuildMergedComponents(
	const TArray<const FFragInstance*>& Instances,
	const FFragImportResult& Result,
	AActor* OwnerActor,
	const FString& NamePrefix,
	UMaterialInterface* BaseMaterial,
	UMaterialInterface* TranslucentMaterial,
	UMaterialInterface* GlassMaterial,
	TMap<uint32, UMaterialInterface*>& MaterialCache)
{
	if (!OwnerActor || !OwnerActor->GetRootComponent() || Instances.Num() == 0)
	{
		return 0;
	}
	// Elements are bucketed by everything that would make them a separate draw
	// call anyway: IFC category, resolved colour, opacity and material class.
	// Colour is part of the key so a merged mesh shades exactly like the
	// unmerged one — vertex colours stay uniform within a bucket.
	struct FMergeBucket
	{
		FString Category;
		FLinearColor Color = FLinearColor::White;
		float Opacity = 1.0f;
		bool bIsGlass = false;
		UMaterialInterface* Material = nullptr;
		TArray<const FFragInstance*> Instances;
	};

	TMap<FString, FMergeBucket> Buckets;

	for (const FFragInstance* InstancePtr : Instances)
	{
		const FFragInstance& Instance = *InstancePtr;
		if (!Result.Geometries.IsValidIndex(Instance.GeometryIndex))
		{
			continue;
		}

		// Material selection must match the per-element path exactly, or a merged
		// element shades differently from the same element unmerged.
		UMaterialInterface* TargetMaterial = BaseMaterial;
		bool bIsGlass = false;
		float AdjustedOpacity = Instance.Opacity;

		const bool bIsBlueColor = (Instance.Color.B > Instance.Color.R + 0.15f && Instance.Color.B > Instance.Color.G + 0.05f);

		if (Instance.Opacity < 0.99f || bIsBlueColor)
		{
			if (bIsBlueColor || Instance.Opacity < 0.5f)
			{
				TargetMaterial = GlassMaterial;
				bIsGlass = true;
				if (Instance.Opacity > 0.99f)
				{
					AdjustedOpacity = 0.5f;
				}
			}
			else
			{
				TargetMaterial = TranslucentMaterial;
			}
		}

		// Fall back rather than leaving a null slot, which renders as default grey.
		if (!TargetMaterial)
		{
			TargetMaterial = BaseMaterial;
		}

		const FString Category = Instance.Category.IsEmpty() ? TEXT("Unclassified") : Instance.Category;
		const FString Key = FString::Printf(TEXT("%s|%02X%02X%02X|%d|%d"),
			*Category,
			(uint8)FMath::Clamp(Instance.Color.R * 255.0f, 0.0f, 255.0f),
			(uint8)FMath::Clamp(Instance.Color.G * 255.0f, 0.0f, 255.0f),
			(uint8)FMath::Clamp(Instance.Color.B * 255.0f, 0.0f, 255.0f),
			FMath::RoundToInt(AdjustedOpacity * 100.0f),
			bIsGlass ? 1 : 0);

		FMergeBucket& Bucket = Buckets.FindOrAdd(Key);
		if (Bucket.Instances.Num() == 0)
		{
			Bucket.Category = Category;
			Bucket.Color = Instance.Color;
			Bucket.Opacity = AdjustedOpacity;
			Bucket.bIsGlass = bIsGlass;
			Bucket.Material = TargetMaterial;
		}
		Bucket.Instances.Add(&Instance);
	}

	int32 ComponentCount = 0;
	int32 ChunksBuilt = 0;

	for (const TPair<FString, FMergeBucket>& BucketPair : Buckets)
	{
		const FMergeBucket& Bucket = BucketPair.Value;

		FLinearColor CorrectedColor;
		CorrectedColor.R = FMath::Pow(Bucket.Color.R, 2.2f);
		CorrectedColor.G = FMath::Pow(Bucket.Color.G, 2.2f);
		CorrectedColor.B = FMath::Pow(Bucket.Color.B, 2.2f);
		CorrectedColor.A = Bucket.Color.A;

		UMaterialInterface* TargetMID = GetOrCreateMaterial(
			Bucket.Material, CorrectedColor, Bucket.Opacity, /*bDoubleSided*/ true, MaterialCache, Bucket.bIsGlass);

		// Sanitise the category once; it becomes part of every component name.
		FString SafeCategory = Bucket.Category;
		for (int32 i = 0; i < SafeCategory.Len(); ++i)
		{
			TCHAR& c = SafeCategory[i];
			if (!FChar::IsAlnum(c) && c != TEXT('_'))
			{
				c = TEXT('_');
			}
		}

		TArray<FFragMergePart> Parts;
		TArray<int32> PartMetadataIndices;
		int64 ChunkVertices = 0;
		int64 ChunkIndices = 0;
		int32 ChunkIndex = 0;

		auto FlushChunk = [&]()
		{
			if (Parts.Num() == 0)
			{
				return;
			}

			// Node labels repeat across the model (every roof has an IFCSLAB
			// child), so the name needs a serial or a later mesh silently
			// clobbers an earlier one sharing the outer.
			const FString MeshName = FString::Printf(TEXT("SM_Merged_%s_%d"),
				*SafeCategory, MergedMeshSerial++);

			TArray<int32> TriangleStarts;
			bool bTriangleOrderPreserved = false;
			UStaticMesh* MergedMesh = CreateMergedStaticMesh(
				Parts, MeshName, CorrectedColor, Bucket.Opacity, TargetMID, &TriangleStarts,
				&bTriangleOrderPreserved, OwnerActor);

			if (MergedMesh)
			{
				const FString CompName = FString::Printf(TEXT("SMC_Merged_%s_%d_%d"),
					*SafeCategory, ComponentCount, ChunkIndex);

				UStaticMeshComponent* MeshComponent = NewObject<UStaticMeshComponent>(OwnerActor, *CompName);
				MeshComponent->SetStaticMesh(MergedMesh);
				MeshComponent->SetupAttachment(OwnerActor->GetRootComponent());
				MeshComponent->SetMobility(EComponentMobility::Static);
				MeshComponent->SetMaterial(0, TargetMID);
				ApplyPickingCollision(MeshComponent, bPickingEnabled, PickingChannel);
				MeshComponent->RegisterComponent();

				MergedMeshes.Add(MeshComponent);

				// Keep the merged elements addressable. The part index also rides in UV0,
				// so the map stays usable even when the build reordered the triangles.
				if (TriangleStarts.Num() == PartMetadataIndices.Num())
				{
					FFragMergedElementMap& FaceMap = MergedElementMetadata.FindOrAdd(MeshComponent);
					FaceMap.TriangleStarts = MoveTemp(TriangleStarts);
					FaceMap.MetadataIndices = PartMetadataIndices;
					FaceMap.bTriangleOrderPreserved = bTriangleOrderPreserved;
				}

				ChunksBuilt++;
				ChunkIndex++;
			}

			Parts.Reset();
			PartMetadataIndices.Reset();
			ChunkVertices = 0;
			ChunkIndices = 0;
		};

		for (const FFragInstance* Instance : Bucket.Instances)
		{
			const FFragGeometry& Geometry = Result.Geometries[Instance->GeometryIndex];
			if (Geometry.Positions.Num() == 0 || Geometry.Indices.Num() < 3)
			{
				continue;
			}

			// Vertices alone do not bound the chunk. A shell whose faces all reference
			// the same few points dedups to a handful of vertices while carrying
			// millions of indices, and every instance of it re-adds the whole index
			// list — so a bucket could grow past what int32 could even count while
			// ChunkVertices stayed in the hundreds.
			if (ChunkVertices > 0
				&& (ChunkVertices + Geometry.Positions.Num() > GMaxMergedVertices
					|| ChunkIndices + Geometry.Indices.Num() > GMaxMergedIndices))
			{
				FlushChunk();
			}

			FFragMergePart Part;
			Part.Geometry = &Geometry;
			Part.Transform = Instance->Transform;
			Parts.Add(Part);

			const int32* MetadataIndex = LocalIdToMetadataIndex.Find(Instance->LocalId);
			PartMetadataIndices.Add(MetadataIndex ? *MetadataIndex : INDEX_NONE);

			ChunkVertices += Geometry.Positions.Num();
			ChunkIndices += Geometry.Indices.Num();
		}

		FlushChunk();
		ComponentCount++;
	}

	return ChunksBuilt;
}

// ─────────────────────────────────────────────────────────────────────────────
// IFC Metadata
// ─────────────────────────────────────────────────────────────────────────────

void AFragmentsActor::BuildMetadataTables(const FFragImportResult& Result)
{
	ItemMetadata.Reset();
	LocalIdToMetadataIndex.Reset();
	GuidToMetadataIndex.Reset();
	InstanceMetadata.Reset();
	MergedElementMetadata.Reset();

	if (Result.Items.Num() == 0)
	{
		return;
	}

	// Only elements that actually render are worth keeping on the actor; the
	// property sets and type objects they reference have already been folded in.
	ItemMetadata.Reserve(FMath::Min(Result.Instances.Num(), Result.Items.Num()));

	for (const FFragInstance& Instance : Result.Instances)
	{
		if (LocalIdToMetadataIndex.Contains(Instance.LocalId))
		{
			continue;
		}

		const FFragItemMetadata* Item = Result.FindItem(Instance.LocalId);
		if (!Item)
		{
			continue;
		}

		const int32 Index = ItemMetadata.Add(*Item);
		LocalIdToMetadataIndex.Add(Instance.LocalId, Index);

		if (!Item->GUID.IsEmpty())
		{
			GuidToMetadataIndex.Add(Item->GUID, Index);
		}
	}

}

bool AFragmentsActor::GetMetadataByLocalId(int32 LocalId, FFragItemMetadata& OutItem) const
{
	if (const int32* Index = LocalIdToMetadataIndex.Find(LocalId))
	{
		if (ItemMetadata.IsValidIndex(*Index))
		{
			OutItem = ItemMetadata[*Index];
			return true;
		}
	}

	OutItem = FFragItemMetadata();
	return false;
}

bool AFragmentsActor::GetMetadataByGuid(const FString& Guid, FFragItemMetadata& OutItem) const
{
	if (const int32* Index = GuidToMetadataIndex.Find(Guid))
	{
		if (ItemMetadata.IsValidIndex(*Index))
		{
			OutItem = ItemMetadata[*Index];
			return true;
		}
	}

	OutItem = FFragItemMetadata();
	return false;
}

bool AFragmentsActor::GetMetadataForInstance(UInstancedStaticMeshComponent* Component, int32 InstanceIndex, FFragItemMetadata& OutItem) const
{
	if (const FFragInstanceMetadataMap* Map = InstanceMetadata.Find(Component))
	{
		if (Map->MetadataIndices.IsValidIndex(InstanceIndex))
		{
			const int32 Index = Map->MetadataIndices[InstanceIndex];
			if (ItemMetadata.IsValidIndex(Index))
			{
				OutItem = ItemMetadata[Index];
				return true;
			}
		}
	}

	OutItem = FFragItemMetadata();
	return false;
}

bool AFragmentsActor::GetMetadataForFace(UStaticMeshComponent* Component, int32 FaceIndex, FFragItemMetadata& OutItem) const
{
	if (const FFragMergedElementMap* FaceMap = MergedElementMetadata.Find(Component))
	{
		// The editor build runs a vertex-cache optimiser over the index buffer, so a
		// face index from a trace is a position in the optimised order while these
		// starts describe the emission order. Searching one with the other resolves to
		// whichever element happens to sit there — a wrong name and GUID reported with
		// no sign that anything went wrong. Refuse instead; GetMetadataFromHit has the
		// UV path that does survive the reorder.
		if (!FaceMap->bTriangleOrderPreserved)
		{
			OutItem = FFragItemMetadata();
			return false;
		}

		if (FaceIndex >= 0 && FaceMap->TriangleStarts.Num() > 0)
		{
			// Last element whose first triangle is at or before this face.
			int32 Low = 0;
			int32 High = FaceMap->TriangleStarts.Num() - 1;
			int32 Found = INDEX_NONE;

			while (Low <= High)
			{
				const int32 Mid = (Low + High) / 2;
				if (FaceMap->TriangleStarts[Mid] <= FaceIndex)
				{
					Found = Mid;
					Low = Mid + 1;
				}
				else
				{
					High = Mid - 1;
				}
			}

			if (Found != INDEX_NONE && FaceMap->MetadataIndices.IsValidIndex(Found))
			{
				const int32 MetadataIndex = FaceMap->MetadataIndices[Found];
				if (ItemMetadata.IsValidIndex(MetadataIndex))
				{
					OutItem = ItemMetadata[MetadataIndex];
					return true;
				}
			}
		}
	}

	OutItem = FFragItemMetadata();
	return false;
}

bool AFragmentsActor::GetMetadataFromHit(const FHitResult& Hit, FFragItemMetadata& OutItem) const
{
	// Instanced mode: FHitResult::Item carries the instance index.
	if (UInstancedStaticMeshComponent* ISMC = Cast<UInstancedStaticMeshComponent>(Hit.GetComponent()))
	{
		return GetMetadataForInstance(ISMC, Hit.Item, OutItem);
	}

	// Merged mode: the face index picks the element out of the welded mesh.
	// Requires the trace to return face indices — enable "Support UV From Hit
	// Results" and trace with bReturnFaceIndex, against complex collision.
	if (UStaticMeshComponent* MeshComponent = Cast<UStaticMeshComponent>(Hit.GetComponent()))
	{
		if (const FFragMergedElementMap* FaceMap = MergedElementMetadata.Find(MeshComponent))
		{
			if (FaceMap->bTriangleOrderPreserved)
			{
				return GetMetadataForFace(MeshComponent, Hit.FaceIndex, OutItem);
			}

			// The editor build permuted the triangles, so the face index no longer
			// names an element. The element index the builder wrote into UV0 does
			// survive it: all three corners of a triangle carry the same value, so the
			// interpolated UV at the hit is that value exactly. Reading it needs
			// "Support UV From Hit Results" in Project Settings → Physics, which is the
			// same setting a face index needs in the first place.
			FVector2D PartUV = FVector2D::ZeroVector;
			if (UGameplayStatics::FindCollisionUV(Hit, /*UVChannel*/ 0, PartUV))
			{
				const int32 PartIndex = FFragMeshBuilder::DecodePartIndexFromUV(PartUV);
				if (FaceMap->MetadataIndices.IsValidIndex(PartIndex)
					&& ItemMetadata.IsValidIndex(FaceMap->MetadataIndices[PartIndex]))
				{
					OutItem = ItemMetadata[FaceMap->MetadataIndices[PartIndex]];
					return true;
				}
			}

			OutItem = FFragItemMetadata();
			return false;
		}
	}

	// Hierarchy mode: the element actor carries its own metadata component.
	if (const UFragmentsMetadataComponent* Component = UFragmentsMetadataLibrary::FindMetadataComponent(Hit.GetActor(), /*bSearchAttachParents*/ true))
	{
		OutItem = Component->ItemData;
		return true;
	}

	OutItem = FFragItemMetadata();
	return false;
}

TArray<int32> AFragmentsActor::FindItemsByAttribute(const FString& AttributeName, const FString& AttributeValue, bool bExactMatch) const
{
	TArray<int32> Found;

	auto Matches = [&AttributeValue, bExactMatch](const FString& Value)
	{
		if (AttributeValue.IsEmpty())
		{
			return true;
		}
		return bExactMatch
			? Value.Equals(AttributeValue, ESearchCase::IgnoreCase)
			: Value.Contains(AttributeValue, ESearchCase::IgnoreCase);
	};

	for (const FFragItemMetadata& Item : ItemMetadata)
	{
		bool bFound = false;

		for (const FFragAttribute& Attribute : Item.Attributes)
		{
			if (Attribute.Name.Equals(AttributeName, ESearchCase::IgnoreCase) && Matches(Attribute.Value))
			{
				bFound = true;
				break;
			}
		}

		if (!bFound)
		{
			for (const FFragPropertySet& Set : Item.PropertySets)
			{
				for (const FFragAttribute& Property : Set.Properties)
				{
					if (Property.Name.Equals(AttributeName, ESearchCase::IgnoreCase) && Matches(Property.Value))
					{
						bFound = true;
						break;
					}
				}
				if (bFound)
				{
					break;
				}
			}
		}

		if (bFound)
		{
			Found.Add(Item.LocalId);
		}
	}

	return Found;
}

TArray<int32> AFragmentsActor::FindItemsByCategory(const FString& Category) const
{
	TArray<int32> Found;
	for (const FFragItemMetadata& Item : ItemMetadata)
	{
		if (Item.Category.Equals(Category, ESearchCase::IgnoreCase))
		{
			Found.Add(Item.LocalId);
		}
	}
	return Found;
}

TMap<FString, int32> AFragmentsActor::GetCategoryCounts() const
{
	TMap<FString, int32> Counts;
	for (const FFragItemMetadata& Item : ItemMetadata)
	{
		if (!Item.Category.IsEmpty())
		{
			Counts.FindOrAdd(Item.Category)++;
		}
	}
	return Counts;
}

TArray<int32> AFragmentsActor::FindItemsByStorey(const FString& StoreyName) const
{
	TArray<int32> Found;
	for (const FFragItemMetadata& Item : ItemMetadata)
	{
		// The storey node is matched by its own name, not by StoreyName — a storey
		// does not stand on itself. Without this the merged-per-storey mode, which
		// hangs the level's whole geometry on that node, would isolate to nothing.
		const bool bIsTheStorey =
			Item.Category.Equals(TEXT("IFCBUILDINGSTOREY"), ESearchCase::IgnoreCase)
			&& Item.Name.Equals(StoreyName, ESearchCase::IgnoreCase);

		if (bIsTheStorey || Item.StoreyName.Equals(StoreyName, ESearchCase::IgnoreCase))
		{
			Found.Add(Item.LocalId);
		}
	}
	return Found;
}

TMap<FString, int32> AFragmentsActor::GetStoreyCounts() const
{
	TMap<FString, int32> Counts;
	for (const FFragItemMetadata& Item : ItemMetadata)
	{
		if (!Item.StoreyName.IsEmpty())
		{
			Counts.FindOrAdd(Item.StoreyName)++;
		}
	}
	return Counts;
}

// ── Filtering ──────────────────────────────────────────────────────────────────

/** Local id an actor renders, read from the metadata component. -1 when it has none. */
static int32 GetActorLocalId(const AActor* Actor)
{
	if (!IsValid(Actor))
	{
		return -1;
	}

	const UFragmentsMetadataComponent* Component =
		Actor->FindComponentByClass<UFragmentsMetadataComponent>();

	return Component ? Component->ItemData.LocalId : -1;
}

bool AFragmentsActor::SupportsFiltering() const
{
	return FragModeUsesHierarchy(BuiltImportMode) && SpawnedChildActors.Num() > 0;
}

bool AFragmentsActor::SupportsElementFiltering() const
{
	// Merged per storey keeps a level's geometry on one actor, so it can hide a
	// level but never a single element.
	return SupportsFiltering() && BuiltImportMode != EFragImportMode::HierarchyPerStorey;
}

void AFragmentsActor::RegisterFilterActor(int32 LocalId, AActor* Actor)
{
	if (LocalId < 0 || !IsValid(Actor))
	{
		return;
	}
	LocalIdToActors.FindOrAdd(LocalId).Actors.Add(Actor);
}

void AFragmentsActor::EnsureFilterIndex()
{
	if (LocalIdToActors.Num() > 0 || SpawnedChildActors.Num() == 0)
	{
		return;
	}

	for (AActor* Child : SpawnedChildActors)
	{
		RegisterFilterActor(GetActorLocalId(Child), Child);
	}

	if (LocalIdToActors.Num() > 0)
	{
		UE_LOG(LogFragmentsUE, Log,
			TEXT("Rebuilt the filter index for %s from %d spawned actors."),
			*ModelName, SpawnedChildActors.Num());
	}
}

void AFragmentsActor::ApplyActorVisibility(AActor* Actor, bool bVisible)
{
	if (!IsValid(Actor))
	{
		return;
	}

	Actor->SetActorHiddenInGame(!bVisible);

#if WITH_EDITOR
	// The editor viewport ignores bHidden, so it needs telling separately. Neither
	// call touches component visibility, which is what keeps the abstract volumes
	// hidden at build time — IfcSpace and friends — from reappearing on Clear.
	Actor->SetIsTemporarilyHiddenInEditor(!bVisible);
#endif
}

TArray<int32> AFragmentsActor::GetFilterableLocalIds() const
{
	TArray<int32> Ids;

	if (LocalIdToActors.Num() > 0)
	{
		LocalIdToActors.GetKeys(Ids);
		return Ids;
	}

	// Index not built yet. Read the actors directly rather than reporting nothing.
	TSet<int32> Unique;
	for (const AActor* Child : SpawnedChildActors)
	{
		const int32 LocalId = GetActorLocalId(Child);
		if (LocalId >= 0)
		{
			Unique.Add(LocalId);
		}
	}
	return Unique.Array();
}

void AFragmentsActor::SetVisibilityByLocalIds(const TArray<int32>& LocalIds, bool bVisible)
{
	EnsureFilterIndex();

	for (const int32 LocalId : LocalIds)
	{
		const FFragActorList* List = LocalIdToActors.Find(LocalId);
		if (!List)
		{
			continue;
		}

		for (const TObjectPtr<AActor>& Actor : List->Actors)
		{
			ApplyActorVisibility(Actor.Get(), bVisible);
		}

		if (bVisible)
		{
			HiddenLocalIds.Remove(LocalId);
		}
		else
		{
			HiddenLocalIds.Add(LocalId);
		}
	}
}

void AFragmentsActor::ApplyIsolation(const TArray<int32>& VisibleLocalIds)
{
	EnsureFilterIndex();

	if (LocalIdToActors.Num() == 0)
	{
		UE_LOG(LogFragmentsUE, Warning,
			TEXT("%s has no per-element actors to filter. Re-import in one of the Hierarchy modes."),
			*ModelName);
		return;
	}

	if (!SupportsElementFiltering())
	{
		UE_LOG(LogFragmentsUE, Warning,
			TEXT("%s was imported merged per storey, so filtering can only isolate whole levels. ")
			TEXT("Anything finer will hide the level it belongs to."),
			*ModelName);
	}

	const TSet<int32> Visible(VisibleLocalIds);

	HiddenLocalIds.Reset();
	for (const TPair<int32, FFragActorList>& Pair : LocalIdToActors)
	{
		const bool bVisible = Visible.Contains(Pair.Key);
		for (const TObjectPtr<AActor>& Actor : Pair.Value.Actors)
		{
			ApplyActorVisibility(Actor.Get(), bVisible);
		}

		if (!bVisible)
		{
			HiddenLocalIds.Add(Pair.Key);
		}
	}
}

void AFragmentsActor::IsolateLocalIds(const TArray<int32>& LocalIds)
{
	ApplyIsolation(LocalIds);
}

void AFragmentsActor::IsolateByCategory(const FString& Category)
{
	ApplyIsolation(FindItemsByCategory(Category));
}

void AFragmentsActor::IsolateByStorey(const FString& StoreyName)
{
	ApplyIsolation(FindItemsByStorey(StoreyName));
}

void AFragmentsActor::IsolateByAttribute(const FString& AttributeName, const FString& AttributeValue, bool bExactMatch)
{
	ApplyIsolation(FindItemsByAttribute(AttributeName, AttributeValue, bExactMatch));
}

void AFragmentsActor::SetCategoryVisible(const FString& Category, bool bVisible)
{
	SetVisibilityByLocalIds(FindItemsByCategory(Category), bVisible);
}

void AFragmentsActor::SetStoreyVisible(const FString& StoreyName, bool bVisible)
{
	SetVisibilityByLocalIds(FindItemsByStorey(StoreyName), bVisible);
}

void AFragmentsActor::ClearFilter()
{
	EnsureFilterIndex();

	// Every actor, not just the ones this filter hid — a level saved mid-filter
	// comes back with actors hidden and an index that no longer remembers why.
	for (const TPair<int32, FFragActorList>& Pair : LocalIdToActors)
	{
		for (const TObjectPtr<AActor>& Actor : Pair.Value.Actors)
		{
			ApplyActorVisibility(Actor.Get(), true);
		}
	}

	HiddenLocalIds.Reset();
}

/**
 * Whether anything under this node is still waiting to be built.
 *
 * Consumption has to be part of the question, not a separate check further down. The
 * node actor and its IFC metadata — a deep copy of every attribute and property set —
 * are created before the per-instance loop is reached, so testing for geometry alone
 * left the expensive half of a repeated node fully exposed: a file whose children
 * vector points many offsets at one node costs four bytes per repeat and bought a
 * whole actor plus a whole metadata copy each time. It also left behind labelled,
 * metadata-bearing actors containing nothing, which read as real elements.
 */
static bool HasUnbuiltGeometry(
	const FFragSpatialNode& Node,
	const TMap<int32, TArray<const FFragInstance*>>& InstancesByLocalId,
	const TSet<int32>& ConsumedLocalIds)
{
	if (Node.LocalId >= 0
		&& !ConsumedLocalIds.Contains(Node.LocalId)
		&& InstancesByLocalId.Contains(Node.LocalId))
	{
		return true;
	}

	for (const FFragSpatialNode& Child : Node.Children)
	{
		if (HasUnbuiltGeometry(Child, InstancesByLocalId, ConsumedLocalIds)) return true;
	}
	return false;
}

/**
 * Every instance at this node or anywhere beneath it, each taken once.
 *
 * Nothing in the file forbids many spatial nodes from carrying the same local id, and
 * without the visited set each repeat welds the same geometry into the mesh again — a
 * few bytes per extra node buying a full copy of an element, for a picture identical
 * to the one a single copy gives.
 */
static void CollectSubtreeInstances(
	const FFragSpatialNode& Node,
	const TMap<int32, TArray<const FFragInstance*>>& InstancesByLocalId,
	TSet<int32>& ConsumedLocalIds,
	TArray<const FFragInstance*>& OutInstances)
{
	// The `>= 0` matters: bucket nodes all carry -1, so if an instance ever kept that
	// id, the first bucket would consume it and every other bucket in the model would
	// then be suppressed. The parser drops such samples today, but that invariant
	// lives in another file — this keeps the walk correct on its own terms.
	if (Node.LocalId >= 0 && !ConsumedLocalIds.Contains(Node.LocalId))
	{
		if (const TArray<const FFragInstance*>* Found = InstancesByLocalId.Find(Node.LocalId))
		{
			OutInstances.Append(*Found);
			ConsumedLocalIds.Add(Node.LocalId);
		}
	}

	for (const FFragSpatialNode& Child : Node.Children)
	{
		CollectSubtreeInstances(Child, InstancesByLocalId, ConsumedLocalIds, OutInstances);
	}
}

/** Spatial containers group elements; they are never elements themselves. */
static bool IsSpatialContainerCategory(const FString& Category)
{
	return Category.Equals(TEXT("IFCPROJECT"), ESearchCase::IgnoreCase)
		|| Category.Equals(TEXT("IFCSITE"), ESearchCase::IgnoreCase)
		|| Category.Equals(TEXT("IFCBUILDING"), ESearchCase::IgnoreCase)
		|| Category.Equals(TEXT("IFCBUILDINGSTOREY"), ESearchCase::IgnoreCase)
		|| Category.Equals(TEXT("IFCSPACE"), ESearchCase::IgnoreCase);
}

/**
 * True when this node is a real IFC element rather than a spatial container or
 * a category bucket. The walk stops at the first of these and welds everything
 * below it, so an element keeps one actor no matter how many bodies or
 * sub-parts it decomposes into.
 */
static bool IsElementNode(const FFragSpatialNode& Node, const FFragImportResult& Result)
{
	if (Node.LocalId < 0 || !Result.Categories.IsValidIndex(Node.LocalId))
	{
		return false; // a category bucket: labelled, but not an item of its own
	}

	return !IsSpatialContainerCategory(Result.Categories[Node.LocalId]);
}

/** Whether the walk should stop here and weld everything below into one actor. */
static bool ShouldMergeAtNode(
	const FFragSpatialNode& Node,
	const FFragImportResult& Result,
	const TMap<int32, TArray<const FFragInstance*>>& InstancesByLocalId,
	EFragImportMode ImportMode)
{
	switch (ImportMode)
	{
	case EFragImportMode::HierarchyPerStorey:
	{
		// The spatial tree alternates bucket nodes (an IFC class label, no local
		// id) with item nodes (a local id, no label). A storey is therefore
		// identified by its *item* category — matching the bucket label instead
		// would swallow every storey in the building at once.
		if (Node.LocalId < 0)
		{
			return false;
		}
		return Result.Categories.IsValidIndex(Node.LocalId)
			&& Result.Categories[Node.LocalId].Equals(TEXT("IFCBUILDINGSTOREY"), ESearchCase::IgnoreCase);
	}

	case EFragImportMode::HierarchyPerElement:
		return IsElementNode(Node, Result);

	default:
		return false;
	}
}

/**
 * Ceiling on actors from one import. Every spatial node and every body under it
 * becomes an actor in the per-body modes, and both counts are file-controlled: N
 * sibling nodes on one local id used to cost N spawns each. Well past this the editor
 * is unusable regardless of what the file intended, and a merged mode is the answer.
 */
static constexpr int32 GMaxSpawnedActors = 250000;

AActor* AFragmentsActor::SpawnHierarchyNode(
	const FFragSpatialNode& Node,
	AActor* ParentActor,
	const TMap<int32, TArray<const FFragInstance*>>& InstancesByLocalId,
	TSet<int32>& ConsumedLocalIds,
	TMap<int64, UStaticMesh*>& StaticMeshCache,
	TMap<uint32, UMaterialInterface*>& MaterialCache,
	const FFragImportResult& Result,
	const FFragImportOptions& Options,
	UMaterialInterface* BaseMaterial,
	UMaterialInterface* TranslucentMaterial,
	UMaterialInterface* GlassMaterial,
	int32& SpawnCount,
	FScopedSlowTask* SlowTask
)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

#if WITH_EDITOR
	// Latched, because ReceivedUserCancel is not documented to stay true once read —
	// the walk polls it from hundreds of thousands of places.
	if (!bCancelRequested && SlowTask && SlowTask->ShouldCancel())
	{
		bCancelRequested = true;
	}
#endif
	if (bCancelRequested)
	{
		return nullptr;
	}

	if (SpawnedChildActors.Num() >= GMaxSpawnedActors)
	{
		if (!bReportedActorLimit)
		{
			bReportedActorLimit = true;
			UE_LOG(LogFragmentsUE, Error,
				TEXT("Stopped at %d actors — this model asks for more than one actor per body can carry. ")
				TEXT("Re-import in a merged mode to get the rest of it."),
				GMaxSpawnedActors);
		}
		return nullptr;
	}

	// Skip rendering this branch entirely if there is no geometry anywhere beneath it
	if (!HasUnbuiltGeometry(Node, InstancesByLocalId, ConsumedLocalIds))
	{
		return nullptr;
	}

	// ── Determine a clean label for this node ────────────────────────────────
	const bool bHasGeometry = InstancesByLocalId.Contains(Node.LocalId);
	const bool bHasRealName = !Node.Name.IsEmpty();

	// Check if this node is just an anonymous wrapper (e.g. Group_XXXX).
	// We want to KEEP category buckets (like IFCBEAM, IFCBUILDINGELEMENTPROXY) 
	// so they appear as grouping folders inside the floors, matching Datasmith.
	bool bIsAnonymousGroup = !bHasRealName && !bHasGeometry && 
		(Node.Category.IsEmpty() || Node.Category.Equals(TEXT("Group"), ESearchCase::IgnoreCase) || Node.Category.Equals(TEXT("Object"), ESearchCase::IgnoreCase));

	// Transparent pass-through: skip anonymous groups and attach their children directly to the parent.
	if (bIsAnonymousGroup)
	{
		for (const FFragSpatialNode& ChildNode : Node.Children)
		{
			SpawnHierarchyNode(ChildNode, ParentActor, InstancesByLocalId, ConsumedLocalIds, StaticMeshCache, MaterialCache, Result, Options, BaseMaterial, TranslucentMaterial, GlassMaterial, SpawnCount, SlowTask);
		}
		return ParentActor;
	}

	// ── Build a clean Datasmith-style label ───────────────────────────────────
	FString NodeLabel;
	if (bHasRealName)
	{
		NodeLabel = Node.Name;
	}
	else if (Node.ExpressId != 0)
	{
		NodeLabel = FString::Printf(TEXT("%s_%u"), *Node.Category, Node.ExpressId);
	}
	else
	{
		NodeLabel = Node.Category;
	}

	// Datasmith sanitization: replace any character that is not alphanumeric or '-' with '_'
	for (int32 i = 0; i < NodeLabel.Len(); ++i)
	{
		TCHAR& c = NodeLabel[i];
		if (!FChar::IsAlnum(c) && c != TEXT('-') && c != TEXT('_'))
		{
			c = TEXT('_');
		}
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	// 1. Spawn an empty folder actor for this spatial node
	AFragmentsNodeActor* NodeActor = World->SpawnActor<AFragmentsNodeActor>(SpawnParams);
	if (!NodeActor)
	{
		return nullptr;
	}

#if WITH_EDITOR
	// Actor labels are an editor-only concept; packaged builds keep the object name.
	NodeActor->SetActorLabel(NodeLabel);
#endif

	// Spatial nodes carry IFC data of their own — a storey's Elevation, a space's
	// LongName, Pset_BuildingStoreyCommon and so on.
	if (Options.bImportMetadata && Options.bAttachMetadataComponents)
	{
		if (const FFragItemMetadata* NodeItem = Result.FindItem(Node.LocalId))
		{
			NodeActor->SetItemMetadata(*NodeItem);
		}
	}

#if WITH_EDITOR
	NodeActor->SetFolderPath(ParentActor->GetFolderPath());
#endif
	NodeActor->GetRootComponent()->AttachToComponent(ParentActor->GetRootComponent(), FAttachmentTransformRules::KeepWorldTransform);
	SpawnedChildActors.Add(NodeActor);

	// Registered even though a node usually holds no geometry: in the merged modes
	// this actor *is* where the subtree's meshes end up, and hiding it is the only
	// way to filter them. On a bare folder node the entry costs nothing.
	RegisterFilterActor(Node.LocalId, NodeActor);

	// 2a. Scoped merge: weld this whole subtree into the node actor and stop.
	// The node keeps its own label, folder and IFC metadata; the elements below
	// it become geometry without identity.
	if (ShouldMergeAtNode(Node, Result, InstancesByLocalId, Options.ImportMode))
	{
		TArray<const FFragInstance*> SubtreeInstances;
		CollectSubtreeInstances(Node, InstancesByLocalId, ConsumedLocalIds, SubtreeInstances);

		if (SubtreeInstances.Num() > 0)
		{
			const int32 Built = BuildMergedComponents(
				SubtreeInstances, Result, NodeActor, NodeLabel,
				BaseMaterial, TranslucentMaterial, GlassMaterial, MaterialCache);

			SpawnCount += SubtreeInstances.Num();
#if WITH_EDITOR
			if (SlowTask)
			{
				SlowTask->EnterProgressFrame(static_cast<float>(SubtreeInstances.Num()));
			}
#endif
		}

		return NodeActor;
	}

	// 2. Spawn geometry leaf actors (StaticMeshActors) for instances at this node.
	// A local id already realised elsewhere in the walk is skipped: the second node
	// carrying it would spawn the same bodies at the same transforms again.
	const TArray<const FFragInstance*>* InstancesForNode =
		(Node.LocalId < 0 || ConsumedLocalIds.Contains(Node.LocalId))
			? nullptr
			: InstancesByLocalId.Find(Node.LocalId);
	if (InstancesForNode && InstancesForNode->Num() > 0)
	{
		ConsumedLocalIds.Add(Node.LocalId);

		int32 InstanceIndex = 1;
		for (const FFragInstance* Inst : *InstancesForNode)
		{
#if WITH_EDITOR
			if (SlowTask)
			{
				SlowTask->EnterProgressFrame(1.0f);
				if (!bCancelRequested && SlowTask->ShouldCancel())
				{
					bCancelRequested = true;
				}
			}
#endif
			if (bCancelRequested)
			{
				break;
			}

			if (SpawnedChildActors.Num() >= GMaxSpawnedActors)
			{
				if (!bReportedActorLimit)
				{
					bReportedActorLimit = true;
					UE_LOG(LogFragmentsUE, Error,
						TEXT("Stopped at %d actors — this model asks for more than one actor per body can carry. ")
						TEXT("Re-import in a merged mode to get the rest of it."),
						GMaxSpawnedActors);
				}
				break;
			}

			AFragmentsElementActor* SMA = World->SpawnActor<AFragmentsElementActor>(SpawnParams);
			if (!SMA) continue;

			if (Options.bImportMetadata && Options.bAttachMetadataComponents)
			{
				if (const FFragItemMetadata* ElementItem = Result.FindItem(Inst->LocalId))
				{
					SMA->SetItemMetadata(*ElementItem);
				}
			}

#if WITH_EDITOR
			// Labels exist only in the editor, so build them only there — otherwise
			// the string and its counter are unused locals in a packaged build.
			{
				// Leaf label: if no name, fallback to Category_LocalId
				FString LeafLabel = Inst->Name;
				if (LeafLabel.IsEmpty())
				{
					LeafLabel = FString::Printf(TEXT("%s_%d"), Inst->Category.IsEmpty() ? TEXT("Element") : *Inst->Category, Inst->LocalId);
				}

				// Datasmith sanitization
				for (int32 i = 0; i < LeafLabel.Len(); ++i)
				{
					TCHAR& c = LeafLabel[i];
					if (!FChar::IsAlnum(c) && c != TEXT('-') && c != TEXT('_'))
					{
						c = TEXT('_');
					}
				}

				if (InstancesForNode->Num() > 1)
				{
					LeafLabel = FString::Printf(TEXT("%s_body%d"), *LeafLabel, InstanceIndex++);
				}

				SMA->SetActorLabel(LeafLabel);
			}
#endif

			UStaticMeshComponent* SMC = SMA->GetStaticMeshComponent();
			SMC->SetMobility(EComponentMobility::Static);

			// Unconditionally, because AStaticMeshActor's constructor leaves this
			// component on BlockAll — an element whose mesh failed to build would
			// otherwise keep it and block the player with invisible geometry.
			SMA->SetActorEnableCollision(bPickingEnabled);
			ApplyPickingCollision(SMC, bPickingEnabled, PickingChannel);

			// Determine TargetMaterial first
			UMaterialInterface* TargetMaterial = BaseMaterial;
			bool bIsGlass = false;
			float AdjustedOpacity = Inst->Opacity;

			bool bIsBlueColor = (Inst->Color.B > Inst->Color.R + 0.15f && Inst->Color.B > Inst->Color.G + 0.05f);

			if (Inst->Opacity < 0.99f || bIsBlueColor)
			{
				if (bIsBlueColor || Inst->Opacity < 0.5f)
				{
					TargetMaterial = GlassMaterial;
					bIsGlass = true;
					if (Inst->Opacity > 0.99f) AdjustedOpacity = 0.5f;
				}
				else
				{
					TargetMaterial = TranslucentMaterial;
				}
			}

			// Get or build static mesh
			UStaticMesh* StaticMesh = nullptr;
			int64 Key = (static_cast<int64>(Inst->GeometryIndex) << 32) | static_cast<uint32>(Inst->MaterialIndex);
			
			FLinearColor CorrectedColor;
			CorrectedColor.R = FMath::Pow(Inst->Color.R, 2.2f);
			CorrectedColor.G = FMath::Pow(Inst->Color.G, 2.2f);
			CorrectedColor.B = FMath::Pow(Inst->Color.B, 2.2f);
			CorrectedColor.A = Inst->Color.A;
			
			if (UStaticMesh** CachedMesh = StaticMeshCache.Find(Key))
			{
				StaticMesh = *CachedMesh;
			}
			else if (Inst->GeometryIndex >= 0 && Inst->GeometryIndex < Result.Geometries.Num())
			{
				FString MeshNameStr = FString::Printf(TEXT("SM_FragGeom_%d_Mat_%d"), Inst->GeometryIndex, Inst->MaterialIndex);
				UMaterialInterface* TargetMID = GetOrCreateMaterial(TargetMaterial, CorrectedColor, AdjustedOpacity, Inst->bDoubleSided, MaterialCache, bIsGlass);
				StaticMesh = CreateStaticMesh(Result.Geometries[Inst->GeometryIndex], MeshNameStr, CorrectedColor, AdjustedOpacity, TargetMID);
				if (StaticMesh)
				{
					StaticMeshCache.Add(Key, StaticMesh);
				}
			}

			if (StaticMesh)
			{
				SMC->SetStaticMesh(StaticMesh);
				SMA->SetActorTransform(Inst->Transform);
				
				UMaterialInterface* TargetMID = GetOrCreateMaterial(TargetMaterial, CorrectedColor, AdjustedOpacity, Inst->bDoubleSided, MaterialCache, bIsGlass);
				SMC->SetMaterial(0, TargetMID);
				
				// Only hide exact matches for abstract organizational volumes
				bool bIsVolume = Inst->Category.Equals(TEXT("IfcSpace"), ESearchCase::IgnoreCase) || 
								 Inst->Category.Equals(TEXT("IfcSite"), ESearchCase::IgnoreCase) ||
								 Inst->Category.Equals(TEXT("IfcBuilding"), ESearchCase::IgnoreCase) ||
								 Inst->Category.Equals(TEXT("IfcOpeningElement"), ESearchCase::IgnoreCase) ||
								 Inst->Category.Equals(TEXT("IfcAnnotation"), ESearchCase::IgnoreCase) ||
								 Inst->Category.Equals(TEXT("IfcOpening"), ESearchCase::IgnoreCase);
								
				if (bIsVolume)
				{
					SMC->SetVisibility(false);
					SMC->SetHiddenInGame(true);
				}
				else if (bIsGlass)
				{
					UE_LOG(LogFragmentsUE, Warning, TEXT("WINDOW DEBUG: Applied Glass Material to actor %s"), *SMA->GetName());
				}
			}
			
#if WITH_EDITOR
			SMA->SetFolderPath(NodeActor->GetFolderPath());
#endif
			SMA->GetRootComponent()->AttachToComponent(NodeActor->GetRootComponent(), FAttachmentTransformRules::KeepWorldTransform);
			SpawnedChildActors.Add(SMA);
			RegisterFilterActor(Inst->LocalId, SMA);

			SpawnCount++;
			if (SpawnCount % 500 == 0)
			{
				FlushRenderingCommands();
			}
		}
	}

	// 3. Recursively spawn children under this node
	for (const FFragSpatialNode& ChildNode : Node.Children)
	{
		SpawnHierarchyNode(ChildNode, NodeActor, InstancesByLocalId, ConsumedLocalIds, StaticMeshCache, MaterialCache, Result, Options, BaseMaterial, TranslucentMaterial, GlassMaterial, SpawnCount, SlowTask);
	}

	return NodeActor;
}

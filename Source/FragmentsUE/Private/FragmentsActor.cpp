#include "FragmentsActor.h"
#include "FragMeshBuilder.h"
#include "FragmentsUEModule.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "ProceduralMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "RenderingThread.h"
#include "Misc/ScopedSlowTask.h"

AFragmentsActor::AFragmentsActor()
{
	PrimaryActorTick.bCanEverTick = false;

	ModelRoot = CreateDefaultSubobject<USceneComponent>(TEXT("ModelRoot"));
	ModelRoot->SetMobility(EComponentMobility::Static);
	RootComponent = ModelRoot;
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
		return;
	}

	TMap<uint32, UMaterialInstanceDynamic*> MaterialCache;
	TMap<int32, UStaticMesh*> StaticMeshCache;

	if (Options.bImportAsHierarchy && Result.SpatialRoot.Children.Num() > 0)
	{
		TMap<int32, TArray<const FFragInstance*>> InstancesByLocalId;
		for (const FFragInstance& Instance : Result.Instances)
		{
			InstancesByLocalId.FindOrAdd(Instance.LocalId).Add(&Instance);
		}
		int32 SpawnCount = 0;
		FScopedSlowTask* SlowTaskPtr = nullptr;
#if WITH_EDITOR
		FScopedSlowTask SlowTask(Result.Instances.Num(), FText::FromString("Building Fragments Hierarchy..."));
		SlowTask.MakeDialog(1.0f);
		SlowTaskPtr = &SlowTask;
#endif
		SpawnHierarchyNode(Result.SpatialRoot, this, InstancesByLocalId, StaticMeshCache, Result, Options, BaseMaterial, TranslucentMaterial, GlassMaterial, MaterialCache, SpawnCount, SlowTaskPtr);
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
		
		UMaterialInstanceDynamic* MID = GetOrCreateMaterial(TargetMaterial, Pair.Color, AdjustedOpacity, Pair.bDoubleSided, MaterialCache, bIsGlass);
		
		if (Options.MeshMode == EFragMeshMode::Static)
		{
			UStaticMesh* StaticMesh = nullptr;
			if (UStaticMesh** CachedMesh = StaticMeshCache.Find(Pair.GeometryIndex))
			{
				StaticMesh = *CachedMesh;
			}
			else
			{
				FString MeshName = FString::Printf(TEXT("SM_FragGeom_%d"), Pair.GeometryIndex);
				StaticMesh = FFragMeshBuilder::BuildStaticMesh(Geom, this, *MeshName);
				if (StaticMesh)
				{
					StaticMeshCache.Add(Pair.GeometryIndex, StaticMesh);
				}
			}

			if (StaticMesh)
			{
				FString CompName = FString::Printf(TEXT("ISMC_%d_Mat_%d"), Pair.GeometryIndex, Pair.MaterialIndex);
				UInstancedStaticMeshComponent* ISMC = NewObject<UInstancedStaticMeshComponent>(this, *CompName);
				ISMC->SetStaticMesh(StaticMesh);
				ISMC->SetupAttachment(ModelRoot);
				
				if (MID)
				{
					ISMC->SetMaterial(0, MID);
					if (bIsGlass)
					{
						UE_LOG(LogFragmentsUE, Warning, TEXT("WINDOW DEBUG: Applied Glass Material (MID) to component %s"), *ISMC->GetName());
					}
				}
				else
				{
					ISMC->SetMaterial(0, TargetMaterial);
					if (bIsGlass)
					{
						UE_LOG(LogFragmentsUE, Warning, TEXT("WINDOW DEBUG: Applied Glass Material (Base) to component %s"), *ISMC->GetName());
					}
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

			FFragMeshBuilder::BuildProceduralMesh(Geom, PMC);

			if (MID)
			{
				PMC->SetMaterial(0, MID);
			}

			PMC->RegisterComponent();
			ProceduralMeshes.Add(Key, PMC);
		}
	}

	// 3. Add instances
	for (const FFragInstance& Instance : Result.Instances)
	{
		int64 Key = (static_cast<int64>(Instance.GeometryIndex) << 32) | static_cast<uint32>(Instance.MaterialIndex);
		if (Options.MeshMode == EFragMeshMode::Static)
		{
			if (UInstancedStaticMeshComponent** ISMC_Ptr = InstancedMeshes.Find(Key))
			{
				(*ISMC_Ptr)->AddInstance(Instance.Transform, true);
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
					FString CompName = FString::Printf(TEXT("PMC_%d_Mat_%d_Inst_%d"), Instance.GeometryIndex, Instance.MaterialIndex, Instance.LocalId);
					UProceduralMeshComponent* NewPMC = DuplicateObject<UProceduralMeshComponent>(TemplatePMC, this, *CompName);
					NewPMC->SetupAttachment(ModelRoot);
					NewPMC->SetWorldTransform(Instance.Transform);
					NewPMC->RegisterComponent();
				}
			}
		}
	}
}

UMaterialInstanceDynamic* AFragmentsActor::GetOrCreateMaterial(
	UMaterialInterface* BaseMaterial, 
	const FLinearColor& Color, 
	float Opacity, 
	bool bDoubleSided,
	TMap<uint32, UMaterialInstanceDynamic*>& OutMaterialCache,
	bool bIsGlass)
{
	if (!BaseMaterial)
	{
		return nullptr;
	}

	uint32 Hash = GetTypeHash(Color);
	Hash = HashCombine(Hash, GetTypeHash(Opacity));
	Hash = HashCombine(Hash, GetTypeHash(bDoubleSided));
	Hash = HashCombine(Hash, GetTypeHash(bIsGlass));

	if (UMaterialInstanceDynamic** FoundMID = OutMaterialCache.Find(Hash))
	{
		return *FoundMID;
	}

	UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(BaseMaterial, this);
	
	FLinearColor FinalColor = Color;

	MID->SetVectorParameterValue(TEXT("BaseColor"), FinalColor);
	MID->SetScalarParameterValue(TEXT("Opacity"), Opacity);

	OutMaterialCache.Add(Hash, MID);
	return MID;
}

static bool HasGeometry(const FFragSpatialNode& Node, const TMap<int32, TArray<const FFragInstance*>>& InstancesByLocalId)
{
	if (InstancesByLocalId.Contains(Node.LocalId)) return true;
	for (const FFragSpatialNode& Child : Node.Children)
	{
		if (HasGeometry(Child, InstancesByLocalId)) return true;
	}
	return false;
}

AActor* AFragmentsActor::SpawnHierarchyNode(
	const FFragSpatialNode& Node, 
	AActor* ParentActor, 
	const TMap<int32, TArray<const FFragInstance*>>& InstancesByLocalId,
	TMap<int32, UStaticMesh*>& StaticMeshCache,
	const FFragImportResult& Result,
	const FFragImportOptions& Options,
	UMaterialInterface* BaseMaterial,
	UMaterialInterface* TranslucentMaterial,
	UMaterialInterface* GlassMaterial,
	TMap<uint32, UMaterialInstanceDynamic*>& MaterialCache,
	int32& SpawnCount,
	FScopedSlowTask* SlowTask
)
{
	UWorld* World = GetWorld();
	if (!World) 
	{
		return nullptr;
	}

	// Skip rendering this branch entirely if there is no geometry anywhere beneath it
	if (!HasGeometry(Node, InstancesByLocalId))
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
			SpawnHierarchyNode(ChildNode, ParentActor, InstancesByLocalId, StaticMeshCache, Result, Options, BaseMaterial, TranslucentMaterial, GlassMaterial, MaterialCache, SpawnCount, SlowTask);
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

	// 1. Spawn an empty AActor folder for this spatial node
	AActor* NodeActor = World->SpawnActor<AActor>(SpawnParams);
	if (!NodeActor) return nullptr;

	NodeActor->SetActorLabel(NodeLabel);

	USceneComponent* SceneComp = NewObject<USceneComponent>(NodeActor);
	SceneComp->SetMobility(EComponentMobility::Static);
	NodeActor->SetRootComponent(SceneComp);
	SceneComp->RegisterComponent();

#if WITH_EDITOR
	NodeActor->SetFolderPath(ParentActor->GetFolderPath());
#endif
	NodeActor->GetRootComponent()->AttachToComponent(ParentActor->GetRootComponent(), FAttachmentTransformRules::KeepWorldTransform);
	SpawnedChildActors.Add(NodeActor);

	// 2. Spawn geometry leaf actors (StaticMeshActors) for instances at this node
	const TArray<const FFragInstance*>* InstancesForNode = InstancesByLocalId.Find(Node.LocalId);
	if (InstancesForNode && InstancesForNode->Num() > 0 && Options.bImportAsHierarchy)
	{
		int32 InstanceIndex = 1;
		for (const FFragInstance* Inst : *InstancesForNode)
		{
#if WITH_EDITOR
			if (SlowTask)
			{
				SlowTask->EnterProgressFrame(1.0f);
			}
#endif
			AStaticMeshActor* SMA = World->SpawnActor<AStaticMeshActor>(SpawnParams);
			if (!SMA) continue;

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
			
			UStaticMeshComponent* SMC = SMA->GetStaticMeshComponent();
			SMC->SetMobility(EComponentMobility::Static);

			// Get or build static mesh
			UStaticMesh* StaticMesh = nullptr;
			if (UStaticMesh** CachedMesh = StaticMeshCache.Find(Inst->GeometryIndex))
			{
				StaticMesh = *CachedMesh;
			}
			else if (Inst->GeometryIndex >= 0 && Inst->GeometryIndex < Result.Geometries.Num())
			{
				FString MeshNameStr = FString::Printf(TEXT("SM_FragGeom_%d"), Inst->GeometryIndex);
				StaticMesh = FFragMeshBuilder::BuildStaticMesh(Result.Geometries[Inst->GeometryIndex], this, FName(*MeshNameStr));
				if (StaticMesh)
				{
					StaticMeshCache.Add(Inst->GeometryIndex, StaticMesh);
				}
			}

			if (StaticMesh)
			{
				SMC->SetStaticMesh(StaticMesh);
				SMA->SetActorTransform(Inst->Transform);
				
				UMaterialInterface* TargetMaterial = BaseMaterial;
				bool bIsGlass = false;
				float AdjustedOpacity = Inst->Opacity;

				bool bIsBlueColor = (Inst->Color.B > Inst->Color.R + 0.15f && Inst->Color.B > Inst->Color.G + 0.05f);

				if (Inst->Opacity < 0.99f || bIsBlueColor)
				{
					if (bIsBlueColor && GlassMaterial)
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

				UMaterialInstanceDynamic* MID = GetOrCreateMaterial(TargetMaterial, Inst->Color, AdjustedOpacity, Inst->bDoubleSided, MaterialCache, bIsGlass);
				if (MID)
				{
					SMC->SetMaterial(0, MID);
				}
			}
			
#if WITH_EDITOR
			SMA->SetFolderPath(NodeActor->GetFolderPath());
#endif
			SMA->GetRootComponent()->AttachToComponent(NodeActor->GetRootComponent(), FAttachmentTransformRules::KeepWorldTransform);
			SpawnedChildActors.Add(SMA);

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
		SpawnHierarchyNode(ChildNode, NodeActor, InstancesByLocalId, StaticMeshCache, Result, Options, BaseMaterial, TranslucentMaterial, GlassMaterial, MaterialCache, SpawnCount, SlowTask);
	}

	return NodeActor;
}

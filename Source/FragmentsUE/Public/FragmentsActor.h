#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FragImportResult.h"
#include "FragImportOptions.h"
#include "FragMetadata.h"
#include "FragmentsActor.generated.h"

class UInstancedStaticMeshComponent;
class UStaticMeshComponent;
class UProceduralMeshComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UFragmentsMetadataComponent;
class AStaticMeshActor;
class FFragAssetFactory;
struct FScopedSlowTask;
struct FFragMergePart;

/**
 * Maps the instance indices of one InstancedStaticMeshComponent to entries in
 * AFragmentsActor::ItemMetadata, so a hit on instance N resolves to its element.
 * Wrapped in a struct because UPROPERTY maps cannot hold containers directly.
 */
USTRUCT()
struct FFragInstanceMetadataMap
{
	GENERATED_BODY()

	/** Parallel to the component's instance indices. -1 when the element has no metadata. */
	UPROPERTY()
	TArray<int32> MetadataIndices;
};

/**
 * Maps face indices of one merged mesh back to the elements welded into it.
 * Merging by category throws away the one-actor-per-element structure, so this
 * is what keeps a merged mesh queryable.
 */
USTRUCT()
struct FFragMergedElementMap
{
	GENERATED_BODY()

	/** First triangle index of each merged element, ascending. */
	UPROPERTY()
	TArray<int32> TriangleStarts;

	/** Index into ItemMetadata for each merged element, parallel to TriangleStarts. */
	UPROPERTY()
	TArray<int32> MetadataIndices;

	/**
	 * Whether TriangleStarts still describes the built mesh.
	 *
	 * The editor build runs a vertex-cache optimiser that permutes triangle order, so
	 * a face index coming back from a trace does not index the order these starts were
	 * recorded in. When this is false the starts are unusable and the lookup falls
	 * back to the element index the builder wrote into UV0, which survives the
	 * reorder — see FFragMeshBuilder::DecodePartIndexFromUV.
	 */
	UPROPERTY()
	bool bTriangleOrderPreserved = false;
};

/**
 * The actors that carry one element's geometry, so a filter can hide it.
 *
 * A list rather than a single actor because the per-body mode spawns one actor
 * per body, and a single IFC element routinely owns several. Wrapped in a struct
 * for the same reason FFragInstanceMetadataMap is — UPROPERTY maps cannot hold
 * containers directly.
 */
USTRUCT()
struct FFragActorList
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<TObjectPtr<AActor>> Actors;
};

UCLASS()
class FRAGMENTSUE_API AFragmentsActor : public AActor
{
	GENERATED_BODY()

public:
	AFragmentsActor();

	/** Build the actor's components or child actors from a parsed result */
	UFUNCTION(BlueprintCallable, Category = "FragmentsUE")
	void BuildFromImportResult(
		const FFragImportResult& Result,
		const FFragImportOptions& Options,
		UMaterialInterface* BaseMaterial,
		UMaterialInterface* TranslucentMaterial = nullptr,
		UMaterialInterface* GlassMaterial = nullptr);

	// ── Model identity ─────────────────────────────────────────────────────────

	/** Unique model GUID from the .frag file. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "FragmentsUE|Model")
	FString ModelGuid;

	/** Model name derived from the source file. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "FragmentsUE|Model")
	FString ModelName;

	/** True when the last build stopped because the user cancelled it. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "FragmentsUE|Model")
	bool bImportWasCancelled = false;

	/** Raw JSON header of the .frag file: IFC schema, authoring tool, units. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "FragmentsUE|Model", meta = (MultiLine = true))
	FString ModelMetadataJson;

	/**
	 * The same header parsed into readable rows — IFC schema, exporter, project /
	 * site / building names and the unit assignment. Shown in the Details panel.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IFC Metadata")
	TObjectPtr<UFragmentsMetadataComponent> ModelMetadataComponent;

	// ── IFC metadata ───────────────────────────────────────────────────────────

	/** IFC metadata for every element of this model that carries geometry. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "FragmentsUE|Metadata")
	TArray<FFragItemMetadata> ItemMetadata;

	/** Metadata for one element by its file-local id. */
	UFUNCTION(BlueprintPure, Category = "FragmentsUE|Metadata")
	bool GetMetadataByLocalId(int32 LocalId, FFragItemMetadata& OutItem) const;

	/** Metadata for one element by its IFC GlobalId. */
	UFUNCTION(BlueprintPure, Category = "FragmentsUE|Metadata")
	bool GetMetadataByGuid(const FString& Guid, FFragItemMetadata& OutItem) const;

	/** Metadata behind one instance of an instanced static mesh component. */
	UFUNCTION(BlueprintPure, Category = "FragmentsUE|Metadata")
	bool GetMetadataForInstance(UInstancedStaticMeshComponent* Component, int32 InstanceIndex, FFragItemMetadata& OutItem) const;

	/**
	 * Metadata behind one face of a merged (Merge by Category) mesh.
	 * Needs the hit to carry a face index — see the note on GetMetadataFromHit.
	 */
	UFUNCTION(BlueprintPure, Category = "FragmentsUE|Metadata")
	bool GetMetadataForFace(UStaticMeshComponent* Component, int32 FaceIndex, FFragItemMetadata& OutItem) const;

	/**
	 * Metadata for whatever a trace hit — the one call a runtime picker needs.
	 * Resolves both import modes: per-element actors and instanced meshes.
	 */
	UFUNCTION(BlueprintPure, Category = "FragmentsUE|Metadata")
	bool GetMetadataFromHit(const FHitResult& Hit, FFragItemMetadata& OutItem) const;

	/**
	 * Re-apply the picking collision policy to everything this actor built.
	 *
	 * On: query-only collision answering TraceChannel and nothing else.
	 * Off: no collision at all.
	 *
	 * Movement channels stay ignored either way, so this cannot make the model solid.
	 * Use it to flip picking without re-importing.
	 */
	UFUNCTION(BlueprintCallable, Category = "FragmentsUE|Picking")
	void SetElementPickingEnabled(bool bEnabled, TEnumAsByte<ECollisionChannel> TraceChannel = ECC_Visibility);

	/** Local ids of every element whose attribute or property matches. */
	UFUNCTION(BlueprintCallable, Category = "FragmentsUE|Metadata")
	TArray<int32> FindItemsByAttribute(const FString& AttributeName, const FString& AttributeValue, bool bExactMatch = true) const;

	/** Local ids of every element of an IFC category, e.g. "IFCWALLSTANDARDCASE". */
	UFUNCTION(BlueprintCallable, Category = "FragmentsUE|Metadata")
	TArray<int32> FindItemsByCategory(const FString& Category) const;

	/** Every IFC category present in this model, with how many elements each has. */
	UFUNCTION(BlueprintCallable, Category = "FragmentsUE|Metadata")
	TMap<FString, int32> GetCategoryCounts() const;

	/**
	 * Local ids of every element standing on a building storey.
	 *
	 * The storey node itself is included, because the merged modes hang that
	 * storey's geometry on the node actor rather than on the elements.
	 */
	UFUNCTION(BlueprintCallable, Category = "FragmentsUE|Metadata")
	TArray<int32> FindItemsByStorey(const FString& StoreyName) const;

	/** Every building storey present in this model, with how many elements each holds. */
	UFUNCTION(BlueprintCallable, Category = "FragmentsUE|Metadata")
	TMap<FString, int32> GetStoreyCounts() const;

	// ── Filtering ──────────────────────────────────────────────────────────────
	//
	// Hiding works on the actors the hierarchy modes spawn, so what can be filtered
	// follows the import mode: per-body and per-element can hide one element, per
	// storey can only hide a whole level, and the modes with no hierarchy at all
	// have nothing to hide. SupportsElementFiltering answers that for the model in
	// hand — see EFragImportMode.

	/** True when this model was built in a mode that keeps one actor per element. */
	UFUNCTION(BlueprintPure, Category = "FragmentsUE|Filter")
	bool SupportsElementFiltering() const;

	/** True when this model has any actors to hide at all. */
	UFUNCTION(BlueprintPure, Category = "FragmentsUE|Filter")
	bool SupportsFiltering() const;

	/** Show or hide the elements named by local id. Ids with no actor are skipped. */
	UFUNCTION(BlueprintCallable, Category = "FragmentsUE|Filter")
	void SetVisibilityByLocalIds(const TArray<int32>& LocalIds, bool bVisible);

	/** Show only these elements and hide every other one. */
	UFUNCTION(BlueprintCallable, Category = "FragmentsUE|Filter")
	void IsolateLocalIds(const TArray<int32>& LocalIds);

	/** Show only the elements of one IFC category, e.g. "IFCDOOR". */
	UFUNCTION(BlueprintCallable, Category = "FragmentsUE|Filter")
	void IsolateByCategory(const FString& Category);

	/** Show only the elements standing on one building storey. */
	UFUNCTION(BlueprintCallable, Category = "FragmentsUE|Filter")
	void IsolateByStorey(const FString& StoreyName);

	/** Show only the elements whose attribute or property matches. */
	UFUNCTION(BlueprintCallable, Category = "FragmentsUE|Filter")
	void IsolateByAttribute(const FString& AttributeName, const FString& AttributeValue, bool bExactMatch = true);

	/** Show or hide one IFC category without disturbing the rest of the filter. */
	UFUNCTION(BlueprintCallable, Category = "FragmentsUE|Filter")
	void SetCategoryVisible(const FString& Category, bool bVisible);

	/** Show or hide one building storey without disturbing the rest of the filter. */
	UFUNCTION(BlueprintCallable, Category = "FragmentsUE|Filter")
	void SetStoreyVisible(const FString& StoreyName, bool bVisible);

	/** Bring the whole model back. */
	UFUNCTION(BlueprintCallable, Category = "FragmentsUE|Filter")
	void ClearFilter();

	/** True when anything is currently hidden by a filter. */
	UFUNCTION(BlueprintPure, Category = "FragmentsUE|Filter")
	bool IsFilterActive() const { return HiddenLocalIds.Num() > 0; }

	/** How many elements the current filter is hiding. */
	UFUNCTION(BlueprintPure, Category = "FragmentsUE|Filter")
	int32 GetHiddenCount() const { return HiddenLocalIds.Num(); }

	/** Local ids the filter can actually act on, i.e. those that own an actor. */
	UFUNCTION(BlueprintPure, Category = "FragmentsUE|Filter")
	TArray<int32> GetFilterableLocalIds() const;

	/** True when this local id is hidden by the current filter. */
	UFUNCTION(BlueprintPure, Category = "FragmentsUE|Filter")
	bool IsLocalIdHidden(int32 LocalId) const { return HiddenLocalIds.Contains(LocalId); }

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "FragmentsUE")
	USceneComponent* ModelRoot;

	/** Map of (GeometryIndex << 32 | MaterialIndex) to its corresponding ISMC (for Static mesh mode) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "FragmentsUE")
	TMap<int64, UInstancedStaticMeshComponent*> InstancedMeshes;

	/** Map of (GeometryIndex << 32 | MaterialIndex) to ProceduralMesh (for Dynamic mesh mode) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "FragmentsUE")
	TMap<int64, UProceduralMeshComponent*> ProceduralMeshes;

	/** List of spawned child actors for hierarchical mode, to clean them up if needed */
	UPROPERTY()
	TArray<AActor*> SpawnedChildActors;

	UPROPERTY()
	bool bHasBuiltHierarchy = false;

	/** LocalId → index into ItemMetadata. */
	UPROPERTY()
	TMap<int32, int32> LocalIdToMetadataIndex;

	/** IFC GlobalId → index into ItemMetadata. */
	UPROPERTY()
	TMap<FString, int32> GuidToMetadataIndex;

	/** ISM component → per-instance metadata indices, for hit resolution. */
	UPROPERTY()
	TMap<UInstancedStaticMeshComponent*, FFragInstanceMetadataMap> InstanceMetadata;

	/** Merged mesh component → per-face metadata, for hit resolution. */
	UPROPERTY()
	TMap<UStaticMeshComponent*, FFragMergedElementMap> MergedElementMetadata;

	/** LocalId → the actors rendering it, which is what a filter hides. */
	UPROPERTY()
	TMap<int32, FFragActorList> LocalIdToActors;

	/** Local ids the current filter is hiding. Empty means the model is whole. */
	UPROPERTY()
	TSet<int32> HiddenLocalIds;

	/**
	 * Import mode this actor was built with, kept because what can be filtered
	 * depends on it and the options struct does not survive the build.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "FragmentsUE|Model")
	EFragImportMode BuiltImportMode = EFragImportMode::HierarchyPerBody;

	/** Components produced by a merge mode. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "FragmentsUE")
	TArray<UStaticMeshComponent*> MergedMeshes;

	/** Serial for merged mesh names — node labels repeat, object names must not. */
	UPROPERTY()
	int32 MergedMeshSerial = 0;

	virtual void Destroyed() override;

private:
	/** Helper to retrieve or create an MID for a given material state */
	UMaterialInterface* GetOrCreateMaterial(
		UMaterialInterface* BaseMaterial,
		const FLinearColor& Color,
		float Opacity,
		bool bDoubleSided,
		TMap<uint32, UMaterialInterface*>& OutMaterialCache,
		bool bIsGlass = false);

	/** Copy the metadata of every instanced element out of the parse result. */
	void BuildMetadataTables(const FFragImportResult& Result);

	/** Note that this actor renders the given element, so a filter can find it. */
	void RegisterFilterActor(int32 LocalId, AActor* Actor);

	/**
	 * Rebuild LocalIdToActors from the spawned actors when it is missing.
	 *
	 * A level saved before filtering existed has the actors but not the index, and
	 * re-importing to get one would be an absurd thing to ask. The actors carry
	 * their own local id in their metadata component, so the index can be recovered
	 * from them.
	 */
	void EnsureFilterIndex();

	/** Hide or show one actor, in both the editor viewport and a running game. */
	static void ApplyActorVisibility(AActor* Actor, bool bVisible);

	/** Shared body of the isolate calls: show these ids, hide the rest. */
	void ApplyIsolation(const TArray<int32>& VisibleLocalIds);

	/**
	 * Writes generated meshes and materials into the Content Browser while a
	 * build runs. Null when baking is off or outside the editor, which puts
	 * mesh creation back on the transient path.
	 */
	TSharedPtr<FFragAssetFactory> AssetFactory;

	/** Build one mesh, into a saved asset package when baking is on. */
	UStaticMesh* CreateStaticMesh(
		const FFragGeometry& Geometry,
		const FString& BaseName,
		const FLinearColor& Color,
		float Opacity,
		UMaterialInterface* Material);

	/** Build one welded mesh, into a saved asset package when baking is on. */
	UStaticMesh* CreateMergedStaticMesh(
		const TArray<FFragMergePart>& Parts,
		const FString& BaseName,
		const FLinearColor& Color,
		float Opacity,
		UMaterialInterface* Material,
		TArray<int32>* OutTriangleStarts,
		bool* OutTriangleOrderPreserved,
		UObject* FallbackOuter);

	/** Save everything the asset factory created and release it. */
	void FinishAssetCreation();

	/**
	 * Weld a set of instances into as few static mesh components as possible,
	 * attached to OwnerActor. Shared by every merge scope.
	 * @return number of components created.
	 */
	int32 BuildMergedComponents(
		const TArray<const FFragInstance*>& Instances,
		const FFragImportResult& Result,
		AActor* OwnerActor,
		const FString& NamePrefix,
		UMaterialInterface* BaseMaterial,
		UMaterialInterface* TranslucentMaterial,
		UMaterialInterface* GlassMaterial,
		TMap<uint32, UMaterialInterface*>& MaterialCache);

	/**
	 * Recursively spawn actors based on the Spatial Tree.
	 *
	 * @param ConsumedLocalIds  Local ids already realised somewhere in this walk.
	 *                          Nothing in the file forbids many spatial nodes from
	 *                          carrying the same id, and without this each repeat
	 *                          spawns the same geometry again.
	 */
	AActor* SpawnHierarchyNode(
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
		FScopedSlowTask* SlowTask = nullptr);

	/** Set once per build when the actor ceiling stopped the walk, so it is reported once. */
	bool bReportedActorLimit = false;

	/** Options.bEnableElementPicking for the build in progress. */
	bool bPickingEnabled = false;

	/** Options.PickingTraceChannel for the build in progress. */
	TEnumAsByte<ECollisionChannel> PickingChannel = ECC_Visibility;

	/** Latched on the first cancel we observe, since ShouldCancel need not stay true. */
	bool bCancelRequested = false;

	/** Drop everything a cancelled build created instead of saving half a model. */
	void DiscardCancelledBuild();
};

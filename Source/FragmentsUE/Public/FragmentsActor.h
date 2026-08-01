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

USTRUCT()
struct FFragInstanceMetadataMap
{
	GENERATED_BODY()

	/** Parallel to the component's instance indices. -1 when the element has no metadata. */
	UPROPERTY()
	TArray<int32> MetadataIndices;
};

USTRUCT()
struct FFragMergedElementMap
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<int32> TriangleStarts;

	UPROPERTY()
	TArray<int32> MetadataIndices;

	/** False when the editor vertex-cache optimiser permuted triangles; lookup falls back to UV0. */
	UPROPERTY()
	bool bTriangleOrderPreserved = false;
};

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

	UFUNCTION(BlueprintCallable, Category = "FragmentsUE")
	void BuildFromImportResult(
		const FFragImportResult& Result,
		const FFragImportOptions& Options,
		UMaterialInterface* BaseMaterial,
		UMaterialInterface* TranslucentMaterial = nullptr,
		UMaterialInterface* GlassMaterial = nullptr);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "FragmentsUE|Model")
	FString ModelGuid;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "FragmentsUE|Model")
	FString ModelName;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "FragmentsUE|Model")
	bool bImportWasCancelled = false;

	/** Raw JSON header of the .frag file: IFC schema, authoring tool, units. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "FragmentsUE|Model", meta = (MultiLine = true))
	FString ModelMetadataJson;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IFC Metadata")
	TObjectPtr<UFragmentsMetadataComponent> ModelMetadataComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "FragmentsUE|Metadata")
	TArray<FFragItemMetadata> ItemMetadata;

	UFUNCTION(BlueprintPure, Category = "FragmentsUE|Metadata")
	bool GetMetadataByLocalId(int32 LocalId, FFragItemMetadata& OutItem) const;

	UFUNCTION(BlueprintPure, Category = "FragmentsUE|Metadata")
	bool GetMetadataByGuid(const FString& Guid, FFragItemMetadata& OutItem) const;

	UFUNCTION(BlueprintPure, Category = "FragmentsUE|Metadata")
	bool GetMetadataForInstance(UInstancedStaticMeshComponent* Component, int32 InstanceIndex, FFragItemMetadata& OutItem) const;

	UFUNCTION(BlueprintPure, Category = "FragmentsUE|Metadata")
	bool GetMetadataForFace(UStaticMeshComponent* Component, int32 FaceIndex, FFragItemMetadata& OutItem) const;

	UFUNCTION(BlueprintPure, Category = "FragmentsUE|Metadata")
	bool GetMetadataFromHit(const FHitResult& Hit, FFragItemMetadata& OutItem) const;

	/** Query-only collision; movement channels stay ignored, so this never makes the model solid. */
	UFUNCTION(BlueprintCallable, Category = "FragmentsUE|Picking")
	void SetElementPickingEnabled(bool bEnabled, TEnumAsByte<ECollisionChannel> TraceChannel = ECC_Visibility);

	UFUNCTION(BlueprintCallable, Category = "FragmentsUE|Metadata")
	TArray<int32> FindItemsByAttribute(const FString& AttributeName, const FString& AttributeValue, bool bExactMatch = true) const;

	UFUNCTION(BlueprintCallable, Category = "FragmentsUE|Metadata")
	TArray<int32> FindItemsByCategory(const FString& Category) const;

	UFUNCTION(BlueprintCallable, Category = "FragmentsUE|Metadata")
	TMap<FString, int32> GetCategoryCounts() const;

	UFUNCTION(BlueprintCallable, Category = "FragmentsUE|Metadata")
	TArray<int32> FindItemsByStorey(const FString& StoreyName) const;

	UFUNCTION(BlueprintCallable, Category = "FragmentsUE|Metadata")
	TMap<FString, int32> GetStoreyCounts() const;

	UFUNCTION(BlueprintPure, Category = "FragmentsUE|Filter")
	bool SupportsElementFiltering() const;

	UFUNCTION(BlueprintPure, Category = "FragmentsUE|Filter")
	bool SupportsFiltering() const;

	UFUNCTION(BlueprintCallable, Category = "FragmentsUE|Filter")
	void SetVisibilityByLocalIds(const TArray<int32>& LocalIds, bool bVisible);

	UFUNCTION(BlueprintCallable, Category = "FragmentsUE|Filter")
	void IsolateLocalIds(const TArray<int32>& LocalIds);

	UFUNCTION(BlueprintCallable, Category = "FragmentsUE|Filter")
	void IsolateByCategory(const FString& Category);

	UFUNCTION(BlueprintCallable, Category = "FragmentsUE|Filter")
	void IsolateByStorey(const FString& StoreyName);

	UFUNCTION(BlueprintCallable, Category = "FragmentsUE|Filter")
	void IsolateByAttribute(const FString& AttributeName, const FString& AttributeValue, bool bExactMatch = true);

	UFUNCTION(BlueprintCallable, Category = "FragmentsUE|Filter")
	void SetCategoryVisible(const FString& Category, bool bVisible);

	UFUNCTION(BlueprintCallable, Category = "FragmentsUE|Filter")
	void SetStoreyVisible(const FString& StoreyName, bool bVisible);

	UFUNCTION(BlueprintCallable, Category = "FragmentsUE|Filter")
	void ClearFilter();

	UFUNCTION(BlueprintPure, Category = "FragmentsUE|Filter")
	bool IsFilterActive() const { return HiddenLocalIds.Num() > 0; }

	UFUNCTION(BlueprintPure, Category = "FragmentsUE|Filter")
	int32 GetHiddenCount() const { return HiddenLocalIds.Num(); }

	UFUNCTION(BlueprintPure, Category = "FragmentsUE|Filter")
	TArray<int32> GetFilterableLocalIds() const;

	UFUNCTION(BlueprintPure, Category = "FragmentsUE|Filter")
	bool IsLocalIdHidden(int32 LocalId) const { return HiddenLocalIds.Contains(LocalId); }

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "FragmentsUE")
	USceneComponent* ModelRoot;

	/** (GeometryIndex << 32 | MaterialIndex) → ISMC, for Static mesh mode. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "FragmentsUE")
	TMap<int64, UInstancedStaticMeshComponent*> InstancedMeshes;

	/** (GeometryIndex << 32 | MaterialIndex) → ProceduralMesh, for Dynamic mesh mode. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "FragmentsUE")
	TMap<int64, UProceduralMeshComponent*> ProceduralMeshes;

	UPROPERTY()
	TArray<AActor*> SpawnedChildActors;

	UPROPERTY()
	bool bHasBuiltHierarchy = false;

	UPROPERTY()
	TMap<int32, int32> LocalIdToMetadataIndex;

	UPROPERTY()
	TMap<FString, int32> GuidToMetadataIndex;

	UPROPERTY()
	TMap<UInstancedStaticMeshComponent*, FFragInstanceMetadataMap> InstanceMetadata;

	UPROPERTY()
	TMap<UStaticMeshComponent*, FFragMergedElementMap> MergedElementMetadata;

	UPROPERTY()
	TMap<int32, FFragActorList> LocalIdToActors;

	UPROPERTY()
	TSet<int32> HiddenLocalIds;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "FragmentsUE|Model")
	EFragImportMode BuiltImportMode = EFragImportMode::HierarchyPerBody;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "FragmentsUE")
	TArray<UStaticMeshComponent*> MergedMeshes;

	UPROPERTY()
	int32 MergedMeshSerial = 0;

	virtual void Destroyed() override;

private:
	UMaterialInterface* GetOrCreateMaterial(
		UMaterialInterface* BaseMaterial,
		const FLinearColor& Color,
		float Opacity,
		bool bDoubleSided,
		TMap<uint32, UMaterialInterface*>& OutMaterialCache,
		bool bIsGlass = false);

	void BuildMetadataTables(const FFragImportResult& Result);

	void RegisterFilterActor(int32 LocalId, AActor* Actor);

	void EnsureFilterIndex();

	static void ApplyActorVisibility(AActor* Actor, bool bVisible);

	void ApplyIsolation(const TArray<int32>& VisibleLocalIds);

	/** Null when baking is off or outside the editor, which leaves generated meshes transient. */
	TSharedPtr<FFragAssetFactory> AssetFactory;

	UStaticMesh* CreateStaticMesh(
		const FFragGeometry& Geometry,
		const FString& BaseName,
		const FLinearColor& Color,
		float Opacity,
		UMaterialInterface* Material);

	UStaticMesh* CreateMergedStaticMesh(
		const TArray<FFragMergePart>& Parts,
		const FString& BaseName,
		const FLinearColor& Color,
		float Opacity,
		UMaterialInterface* Material,
		TArray<int32>* OutTriangleStarts,
		bool* OutTriangleOrderPreserved,
		UObject* FallbackOuter);

	void FinishAssetCreation();

	int32 BuildMergedComponents(
		const TArray<const FFragInstance*>& Instances,
		const FFragImportResult& Result,
		AActor* OwnerActor,
		const FString& NamePrefix,
		UMaterialInterface* BaseMaterial,
		UMaterialInterface* TranslucentMaterial,
		UMaterialInterface* GlassMaterial,
		TMap<uint32, UMaterialInterface*>& MaterialCache);

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

	bool bReportedActorLimit = false;

	bool bPickingEnabled = false;

	TEnumAsByte<ECollisionChannel> PickingChannel = ECC_Visibility;

	bool bCancelRequested = false;

	void DiscardCancelledBuild();
};

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FragImportResult.h"
#include "FragImportOptions.h"
#include "FragmentsActor.generated.h"

class UInstancedStaticMeshComponent;
class UProceduralMeshComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class AStaticMeshActor;
struct FScopedSlowTask;

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

	virtual void Destroyed() override;

private:
	/** Helper to retrieve or create an MID for a given material state */
	UMaterialInstanceDynamic* GetOrCreateMaterial(
		UMaterialInterface* BaseMaterial, 
		const FLinearColor& Color, 
		float Opacity, 
		bool bDoubleSided,
		TMap<uint32, UMaterialInstanceDynamic*>& OutMaterialCache,
		bool bIsGlass = false);

	/** Recursively spawn actors based on the Spatial Tree */
	AActor* SpawnHierarchyNode(
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
		FScopedSlowTask* SlowTask = nullptr);
};

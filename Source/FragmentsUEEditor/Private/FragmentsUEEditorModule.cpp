#include "FragmentsUEEditorModule.h"
#include "FragmentsUEModule.h"
#include "FragmentsMetadataDetails.h"
#include "SFragmentsFilterPanel.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "Widgets/Docking/SDockTab.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"
#include "Styling/AppStyle.h"
#include "FragmentsMetadataComponent.h"
#include "FragmentsElementActor.h"
#include "FragmentsActor.h"
#include "PropertyEditorModule.h"
#include "Factories/MaterialFactoryNew.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/SavePackage.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionVertexColor.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Containers/Ticker.h"
#include "UObject/StrongObjectPtr.h"

void FFragmentsUEEditorModule::GenerateMaterials(bool bForceRebuild)
{
	// Rooted: a GC pass before the LoadObject calls below would leave this raw pointer dangling.
	TStrongObjectPtr<UMaterialFactoryNew> MaterialFactory(NewObject<UMaterialFactoryNew>());

		FString PackageName = TEXT("/FragmentsUE/M_FragBase");
		FString MaterialName = TEXT("M_FragBase");
		
		UMaterial* BaseMaterialObj = LoadObject<UMaterial>(nullptr, *(PackageName + TEXT(".") + MaterialName));
		UPackage* Package = nullptr;
		const bool bBaseExisted = BaseMaterialObj != nullptr;
		if (bBaseExisted)
		{
			Package = BaseMaterialObj->GetOutermost();
		}
		else
		{
			Package = CreatePackage(*PackageName);
			BaseMaterialObj = (UMaterial*)MaterialFactory->FactoryCreateNew(UMaterial::StaticClass(), Package, FName(*MaterialName), RF_Public | RF_Standalone, nullptr, GWarn);
		}

		// Rebuilding wipes the expression graph and saves over the package, discarding user edits.
		if ((!bBaseExisted || bForceRebuild) && BaseMaterialObj)
		{
			BaseMaterialObj->bUsedWithInstancedStaticMeshes = true;
			BaseMaterialObj->BlendMode = BLEND_Opaque;
			BaseMaterialObj->TwoSided = true;
			
			BaseMaterialObj->GetExpressionCollection().Expressions.Empty();

			UMaterialExpressionVectorParameter* BaseColorExp = NewObject<UMaterialExpressionVectorParameter>(BaseMaterialObj);
			BaseColorExp->ParameterName = TEXT("BaseColor");
			BaseColorExp->DefaultValue = FLinearColor::White;
			BaseMaterialObj->GetExpressionCollection().AddExpression(BaseColorExp);
			
			UMaterialExpressionVertexColor* VertexColorExp = NewObject<UMaterialExpressionVertexColor>(BaseMaterialObj);
			BaseMaterialObj->GetExpressionCollection().AddExpression(VertexColorExp);
			
			UMaterialExpressionComponentMask* BaseColorMask = NewObject<UMaterialExpressionComponentMask>(BaseMaterialObj);
			BaseColorMask->Input.Expression = BaseColorExp;
			BaseColorMask->R = 1;
			BaseColorMask->G = 1;
			BaseColorMask->B = 1;
			BaseColorMask->A = 0;
			BaseMaterialObj->GetExpressionCollection().AddExpression(BaseColorMask);

			UMaterialExpressionMultiply* BaseColorMul = NewObject<UMaterialExpressionMultiply>(BaseMaterialObj);
			BaseColorMul->A.Expression = BaseColorMask;
			BaseColorMul->B.Expression = VertexColorExp;
			BaseMaterialObj->GetExpressionCollection().AddExpression(BaseColorMul);
			
			BaseMaterialObj->GetEditorOnlyData()->BaseColor.Expression = BaseColorMul;

			UMaterialExpressionScalarParameter* OpacityExp = NewObject<UMaterialExpressionScalarParameter>(BaseMaterialObj);
			OpacityExp->ParameterName = TEXT("Opacity");
			OpacityExp->DefaultValue = 1.0f;
			BaseMaterialObj->GetExpressionCollection().AddExpression(OpacityExp);
			BaseMaterialObj->GetEditorOnlyData()->Opacity.Expression = OpacityExp;

			UMaterialExpressionScalarParameter* RoughnessExp = NewObject<UMaterialExpressionScalarParameter>(BaseMaterialObj);
			RoughnessExp->ParameterName = TEXT("Roughness");
			RoughnessExp->DefaultValue = 0.5f;
			BaseMaterialObj->GetExpressionCollection().AddExpression(RoughnessExp);
			BaseMaterialObj->GetEditorOnlyData()->Roughness.Expression = RoughnessExp;

			UMaterialExpressionScalarParameter* SpecularExp = NewObject<UMaterialExpressionScalarParameter>(BaseMaterialObj);
			SpecularExp->ParameterName = TEXT("Specular");
			SpecularExp->DefaultValue = 0.5f; 
			BaseMaterialObj->GetExpressionCollection().AddExpression(SpecularExp);
			BaseMaterialObj->GetEditorOnlyData()->Specular.Expression = SpecularExp;

			UMaterialExpressionScalarParameter* MetallicExp = NewObject<UMaterialExpressionScalarParameter>(BaseMaterialObj);
			MetallicExp->ParameterName = TEXT("Metallic");
			MetallicExp->DefaultValue = 0.0f;
			BaseMaterialObj->GetExpressionCollection().AddExpression(MetallicExp);
			BaseMaterialObj->GetEditorOnlyData()->Metallic.Expression = MetallicExp;

			BaseMaterialObj->PostEditChange();
			FAssetRegistryModule::AssetCreated(BaseMaterialObj);
			
			FString PackageFileName = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());
			FSavePackageArgs SaveArgs;
			SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
			SaveArgs.Error = GWarn;
			UPackage::SavePackage(Package, BaseMaterialObj, *PackageFileName, SaveArgs);

		}

		FString TranslucentPackageName = TEXT("/FragmentsUE/M_FragBase_Translucent");
		FString TranslucentMaterialName = TEXT("M_FragBase_Translucent");
		
		UMaterial* TransMaterialObj = LoadObject<UMaterial>(nullptr, *(TranslucentPackageName + TEXT(".") + TranslucentMaterialName));
		UPackage* TransPackage = nullptr;
		const bool bTransExisted = TransMaterialObj != nullptr;
		if (bTransExisted)
		{
			TransPackage = TransMaterialObj->GetOutermost();
		}
		else
		{
			TransPackage = CreatePackage(*TranslucentPackageName);
			TransMaterialObj = (UMaterial*)MaterialFactory->FactoryCreateNew(UMaterial::StaticClass(), TransPackage, FName(*TranslucentMaterialName), RF_Public | RF_Standalone, nullptr, GWarn);
		}

		if ((!bTransExisted || bForceRebuild) && TransMaterialObj)
		{
			TransMaterialObj->bUsedWithInstancedStaticMeshes = true;
			TransMaterialObj->BlendMode = BLEND_Translucent;
			TransMaterialObj->TranslucencyLightingMode = TLM_Surface;
			TransMaterialObj->TwoSided = true;
			
			TransMaterialObj->GetExpressionCollection().Expressions.Empty();

			UMaterialExpressionVectorParameter* BaseColorExp = NewObject<UMaterialExpressionVectorParameter>(TransMaterialObj);
			BaseColorExp->ParameterName = TEXT("BaseColor");
			BaseColorExp->DefaultValue = FLinearColor::White;
			TransMaterialObj->GetExpressionCollection().AddExpression(BaseColorExp);

			UMaterialExpressionVertexColor* VertexColorExp = NewObject<UMaterialExpressionVertexColor>(TransMaterialObj);
			TransMaterialObj->GetExpressionCollection().AddExpression(VertexColorExp);

			UMaterialExpressionComponentMask* BaseColorMask = NewObject<UMaterialExpressionComponentMask>(TransMaterialObj);
			BaseColorMask->Input.Expression = BaseColorExp;
			BaseColorMask->R = 1;
			BaseColorMask->G = 1;
			BaseColorMask->B = 1;
			BaseColorMask->A = 0;
			TransMaterialObj->GetExpressionCollection().AddExpression(BaseColorMask);

			UMaterialExpressionMultiply* BaseColorMul = NewObject<UMaterialExpressionMultiply>(TransMaterialObj);
			BaseColorMul->A.Expression = BaseColorMask;
			BaseColorMul->B.Expression = VertexColorExp;
			TransMaterialObj->GetExpressionCollection().AddExpression(BaseColorMul);

			TransMaterialObj->GetEditorOnlyData()->BaseColor.Expression = BaseColorMul;

			UMaterialExpressionScalarParameter* OpacityExp = NewObject<UMaterialExpressionScalarParameter>(TransMaterialObj);
			OpacityExp->ParameterName = TEXT("Opacity");
			OpacityExp->DefaultValue = 1.0f;
			TransMaterialObj->GetExpressionCollection().AddExpression(OpacityExp);
			TransMaterialObj->GetEditorOnlyData()->Opacity.Expression = OpacityExp;

			UMaterialExpressionScalarParameter* SpecularExp = NewObject<UMaterialExpressionScalarParameter>(TransMaterialObj);
			SpecularExp->ParameterName = TEXT("Specular");
			SpecularExp->DefaultValue = 1.0f;
			TransMaterialObj->GetExpressionCollection().AddExpression(SpecularExp);
			TransMaterialObj->GetEditorOnlyData()->Specular.Expression = SpecularExp;

			UMaterialExpressionScalarParameter* RoughnessExp = NewObject<UMaterialExpressionScalarParameter>(TransMaterialObj);
			RoughnessExp->ParameterName = TEXT("Roughness");
			RoughnessExp->DefaultValue = 0.1f;
			TransMaterialObj->GetExpressionCollection().AddExpression(RoughnessExp);
			TransMaterialObj->GetEditorOnlyData()->Roughness.Expression = RoughnessExp;

			UMaterialExpressionScalarParameter* MetallicExp = NewObject<UMaterialExpressionScalarParameter>(TransMaterialObj);
			MetallicExp->ParameterName = TEXT("Metallic");
			MetallicExp->DefaultValue = 0.0f;
			TransMaterialObj->GetExpressionCollection().AddExpression(MetallicExp);
			TransMaterialObj->GetEditorOnlyData()->Metallic.Expression = MetallicExp;

			TransMaterialObj->PostEditChange();
			FAssetRegistryModule::AssetCreated(TransMaterialObj);
			
			FString TransPackageFileName = FPackageName::LongPackageNameToFilename(TranslucentPackageName, FPackageName::GetAssetPackageExtension());
			FSavePackageArgs SaveArgs;
			SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
			SaveArgs.Error = GWarn;
			UPackage::SavePackage(TransPackage, TransMaterialObj, *TransPackageFileName, SaveArgs);

		}

		FString GlassPackageName = TEXT("/FragmentsUE/M_FragBase_Glass");
		FString GlassMaterialName = TEXT("M_FragBase_Glass");
		
		UMaterial* GlassMaterialObj = LoadObject<UMaterial>(nullptr, *(GlassPackageName + TEXT(".") + GlassMaterialName));
		UPackage* GlassPackage = nullptr;
		const bool bGlassExisted = GlassMaterialObj != nullptr;
		if (bGlassExisted)
		{
			GlassPackage = GlassMaterialObj->GetOutermost();
		}
		else
		{
			GlassPackage = CreatePackage(*GlassPackageName);
			GlassMaterialObj = (UMaterial*)MaterialFactory->FactoryCreateNew(UMaterial::StaticClass(), GlassPackage, FName(*GlassMaterialName), RF_Public | RF_Standalone, nullptr, GWarn);
		}

		if ((!bGlassExisted || bForceRebuild) && GlassMaterialObj)
		{
			GlassMaterialObj->bUsedWithInstancedStaticMeshes = true;
			GlassMaterialObj->BlendMode = BLEND_Translucent;
			GlassMaterialObj->TranslucencyLightingMode = TLM_Surface;
			GlassMaterialObj->TwoSided = true;
			
			GlassMaterialObj->GetEditorOnlyData()->EmissiveColor.Expression = nullptr;
			GlassMaterialObj->GetEditorOnlyData()->Specular.Expression = nullptr;
			GlassMaterialObj->GetEditorOnlyData()->Roughness.Expression = nullptr;
			GlassMaterialObj->GetEditorOnlyData()->Normal.Expression = nullptr;
			
			GlassMaterialObj->GetExpressionCollection().Expressions.Empty();

			UMaterialExpressionVectorParameter* BaseColorExp = NewObject<UMaterialExpressionVectorParameter>(GlassMaterialObj);
			BaseColorExp->ParameterName = TEXT("BaseColor");
			BaseColorExp->DefaultValue = FLinearColor::White;
			GlassMaterialObj->GetExpressionCollection().AddExpression(BaseColorExp);

			UMaterialExpressionVertexColor* VertexColorExp = NewObject<UMaterialExpressionVertexColor>(GlassMaterialObj);
			GlassMaterialObj->GetExpressionCollection().AddExpression(VertexColorExp);

			UMaterialExpressionComponentMask* BaseColorMask = NewObject<UMaterialExpressionComponentMask>(GlassMaterialObj);
			BaseColorMask->Input.Expression = BaseColorExp;
			BaseColorMask->R = 1;
			BaseColorMask->G = 1;
			BaseColorMask->B = 1;
			BaseColorMask->A = 0;
			GlassMaterialObj->GetExpressionCollection().AddExpression(BaseColorMask);

			UMaterialExpressionMultiply* BaseColorMul = NewObject<UMaterialExpressionMultiply>(GlassMaterialObj);
			BaseColorMul->A.Expression = BaseColorMask;
			BaseColorMul->B.Expression = VertexColorExp;
			GlassMaterialObj->GetExpressionCollection().AddExpression(BaseColorMul);

			GlassMaterialObj->GetEditorOnlyData()->BaseColor.Expression = BaseColorMul;
			
			// Slight emissive glow keeps the tint visible under dim lighting
			UMaterialExpressionMultiply* EmissiveMul = NewObject<UMaterialExpressionMultiply>(GlassMaterialObj);
			EmissiveMul->A.Expression = BaseColorMul;
			EmissiveMul->ConstB = 0.35f;
			GlassMaterialObj->GetExpressionCollection().AddExpression(EmissiveMul);
			GlassMaterialObj->GetEditorOnlyData()->EmissiveColor.Expression = EmissiveMul;

			UMaterialExpressionScalarParameter* OpacityExp = NewObject<UMaterialExpressionScalarParameter>(GlassMaterialObj);
			OpacityExp->ParameterName = TEXT("Opacity");
			OpacityExp->DefaultValue = 0.5f;
			GlassMaterialObj->GetExpressionCollection().AddExpression(OpacityExp);
			GlassMaterialObj->GetEditorOnlyData()->Opacity.Expression = OpacityExp;

			UMaterialExpressionScalarParameter* SpecularExp = NewObject<UMaterialExpressionScalarParameter>(GlassMaterialObj);
			SpecularExp->ParameterName = TEXT("Specular");
			SpecularExp->DefaultValue = 0.2f; // Low specular to avoid the "perfect mirror" effect
			GlassMaterialObj->GetExpressionCollection().AddExpression(SpecularExp);
			GlassMaterialObj->GetEditorOnlyData()->Specular.Expression = SpecularExp;

			UMaterialExpressionScalarParameter* RoughnessExp = NewObject<UMaterialExpressionScalarParameter>(GlassMaterialObj);
			RoughnessExp->ParameterName = TEXT("Roughness");
			RoughnessExp->DefaultValue = 0.1f; // Slightly rough to scatter reflections
			GlassMaterialObj->GetExpressionCollection().AddExpression(RoughnessExp);
			GlassMaterialObj->GetEditorOnlyData()->Roughness.Expression = RoughnessExp;

			UMaterialExpressionScalarParameter* MetallicExp = NewObject<UMaterialExpressionScalarParameter>(GlassMaterialObj);
			MetallicExp->ParameterName = TEXT("Metallic");
			MetallicExp->DefaultValue = 0.0f;
			GlassMaterialObj->GetExpressionCollection().AddExpression(MetallicExp);
			GlassMaterialObj->GetEditorOnlyData()->Metallic.Expression = MetallicExp;

			GlassMaterialObj->PostEditChange();
			FAssetRegistryModule::AssetCreated(GlassMaterialObj);
			
			FString GlassPackageFileName = FPackageName::LongPackageNameToFilename(GlassPackageName, FPackageName::GetAssetPackageExtension());
			FSavePackageArgs SaveArgs;
			SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
			SaveArgs.Error = GWarn;
			UPackage::SavePackage(GlassPackage, GlassMaterialObj, *GlassPackageFileName, SaveArgs);

	}
}

static FAutoConsoleCommand GCmdGenerateMaterial(
	TEXT("FragmentsUE.GenerateMaterial"),
	TEXT("Rebuilds the M_FragBase materials in the plugin content folder, discarding any edits made to them"),
	FConsoleCommandDelegate::CreateLambda([]()
	{
		FFragmentsUEEditorModule::GenerateMaterials(/*bForceRebuild*/ true);
	})
);

void FFragmentsUEEditorModule::StartupModule()
{
	RegisterDetailCustomizations();

	// The core ticker also runs in commandlets, where this would mutate and save content mid-cook.
	if (IsRunningCommandlet())
	{
		return;
	}

	// Registering a nomad tab needs the Slate application, which a commandlet does not have.
	RegisterFilterTab();

	// Deferred ~3s so the editor and its validators are fully initialized
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([](float DeltaTime)
	{
		FFragmentsUEEditorModule::GenerateMaterials(/*bForceRebuild*/ false);
		return false;
	}), 3.0f);
}

void FFragmentsUEEditorModule::ShutdownModule()
{
	UnregisterDetailCustomizations();
	UnregisterFilterTab();
}

#define LOCTEXT_NAMESPACE "FragmentsUEEditor"

void FFragmentsUEEditorModule::RegisterFilterTab()
{
	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
		SFragmentsFilterPanel::TabId,
		FOnSpawnTab::CreateRaw(this, &FFragmentsUEEditorModule::SpawnFilterTab))
		.SetDisplayName(LOCTEXT("FilterTabTitle", "IFC Filter"))
		.SetTooltipText(LOCTEXT("FilterTabTooltip", "Isolate an imported BIM model by level, category or property."))
		.SetGroup(WorkspaceMenu::GetMenuStructure().GetLevelEditorCategory())
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.Layers"));
}

void FFragmentsUEEditorModule::UnregisterFilterTab()
{
	if (FSlateApplication::IsInitialized())
	{
		FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(SFragmentsFilterPanel::TabId);
	}
}

TSharedRef<SDockTab> FFragmentsUEEditorModule::SpawnFilterTab(const FSpawnTabArgs& Args)
{
	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		[
			SNew(SFragmentsFilterPanel)
		];
}

#undef LOCTEXT_NAMESPACE

void FFragmentsUEEditorModule::RegisterDetailCustomizations()
{
	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");

	CustomizedClassNames = {
		UFragmentsMetadataComponent::StaticClass()->GetFName(),
		AFragmentsElementActor::StaticClass()->GetFName(),
		AFragmentsNodeActor::StaticClass()->GetFName(),
		AFragmentsActor::StaticClass()->GetFName()
	};

	for (const FName& ClassName : CustomizedClassNames)
	{
		PropertyModule.RegisterCustomClassLayout(
			ClassName,
			FOnGetDetailCustomizationInstance::CreateStatic(&FFragmentsMetadataDetails::MakeInstance));
	}

	PropertyModule.NotifyCustomizationModuleChanged();
}

void FFragmentsUEEditorModule::UnregisterDetailCustomizations()
{
	if (FPropertyEditorModule* PropertyModule = FModuleManager::GetModulePtr<FPropertyEditorModule>("PropertyEditor"))
	{
		for (const FName& ClassName : CustomizedClassNames)
		{
			PropertyModule->UnregisterCustomClassLayout(ClassName);
		}
		PropertyModule->NotifyCustomizationModuleChanged();
	}

	CustomizedClassNames.Empty();
}

IMPLEMENT_MODULE(FFragmentsUEEditorModule, FragmentsUEEditor)

#include "FragmentsUEEditorModule.h"
#include "FragmentsUEModule.h"
#include "Factories/MaterialFactoryNew.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/SavePackage.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Containers/Ticker.h"
void FFragmentsUEEditorModule::GenerateMaterials()
{
	UMaterialFactoryNew* MaterialFactory = NewObject<UMaterialFactoryNew>();

		// 1. Generate Opaque Material
		FString PackageName = TEXT("/FragmentsUE/M_FragBase");
		FString MaterialName = TEXT("M_FragBase");
		
		UMaterial* BaseMaterialObj = LoadObject<UMaterial>(nullptr, *(PackageName + TEXT(".") + MaterialName));
		UPackage* Package = nullptr;
		if (BaseMaterialObj)
		{
			UE_LOG(LogFragmentsUE, Log, TEXT("M_FragBase exists, updating it..."));
			Package = BaseMaterialObj->GetOutermost();
		}
		else
		{
			UE_LOG(LogFragmentsUE, Log, TEXT("Auto-generating default material..."));
			Package = CreatePackage(*PackageName);
			BaseMaterialObj = (UMaterial*)MaterialFactory->FactoryCreateNew(UMaterial::StaticClass(), Package, FName(*MaterialName), RF_Public | RF_Standalone, nullptr, GWarn);
		}

		if (BaseMaterialObj)
		{
			BaseMaterialObj->bUsedWithInstancedStaticMeshes = true;
			BaseMaterialObj->BlendMode = BLEND_Opaque;
			BaseMaterialObj->TwoSided = true;
			
			BaseMaterialObj->GetExpressionCollection().Expressions.Empty();

			UMaterialExpressionVectorParameter* BaseColorExp = NewObject<UMaterialExpressionVectorParameter>(BaseMaterialObj);
			BaseColorExp->ParameterName = TEXT("BaseColor");
			BaseColorExp->DefaultValue = FLinearColor::White;
			BaseMaterialObj->GetExpressionCollection().AddExpression(BaseColorExp);
			BaseMaterialObj->GetEditorOnlyData()->BaseColor.Expression = BaseColorExp;

			UMaterialExpressionScalarParameter* OpacityExp = NewObject<UMaterialExpressionScalarParameter>(BaseMaterialObj);
			OpacityExp->ParameterName = TEXT("Opacity");
			OpacityExp->DefaultValue = 1.0f;
			BaseMaterialObj->GetExpressionCollection().AddExpression(OpacityExp);
			BaseMaterialObj->GetEditorOnlyData()->Opacity.Expression = OpacityExp;

			UMaterialExpressionScalarParameter* RoughnessExp = NewObject<UMaterialExpressionScalarParameter>(BaseMaterialObj);
			RoughnessExp->ParameterName = TEXT("Roughness");
			RoughnessExp->DefaultValue = 0.85f;
			BaseMaterialObj->GetExpressionCollection().AddExpression(RoughnessExp);
			BaseMaterialObj->GetEditorOnlyData()->Roughness.Expression = RoughnessExp;

			UMaterialExpressionScalarParameter* SpecularExp = NewObject<UMaterialExpressionScalarParameter>(BaseMaterialObj);
			SpecularExp->ParameterName = TEXT("Specular");
			SpecularExp->DefaultValue = 0.0f; // Prevent sun glare from turning light colors pure white
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

			UE_LOG(LogFragmentsUE, Log, TEXT("Auto-generated M_FragBase successfully at %s"), *PackageFileName);
		}

		// 2. Generate Translucent Material
		FString TranslucentPackageName = TEXT("/FragmentsUE/M_FragBase_Translucent");
		FString TranslucentMaterialName = TEXT("M_FragBase_Translucent");
		
		UMaterial* TransMaterialObj = LoadObject<UMaterial>(nullptr, *(TranslucentPackageName + TEXT(".") + TranslucentMaterialName));
		UPackage* TransPackage = nullptr;
		if (TransMaterialObj)
		{
			UE_LOG(LogFragmentsUE, Log, TEXT("M_FragBase_Translucent exists, updating it..."));
			TransPackage = TransMaterialObj->GetOutermost();
		}
		else
		{
			UE_LOG(LogFragmentsUE, Log, TEXT("Auto-generating translucent material..."));
			TransPackage = CreatePackage(*TranslucentPackageName);
			TransMaterialObj = (UMaterial*)MaterialFactory->FactoryCreateNew(UMaterial::StaticClass(), TransPackage, FName(*TranslucentMaterialName), RF_Public | RF_Standalone, nullptr, GWarn);
		}

		if (TransMaterialObj)
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
			TransMaterialObj->GetEditorOnlyData()->BaseColor.Expression = BaseColorExp;

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

			UE_LOG(LogFragmentsUE, Log, TEXT("Auto-generated M_FragBase_Translucent successfully at %s"), *TransPackageFileName);
		}

		// 3. Generate Glass Material (Specific for Glass/Windows)
		FString GlassPackageName = TEXT("/FragmentsUE/M_FragBase_Glass");
		FString GlassMaterialName = TEXT("M_FragBase_Glass");
		
		UMaterial* GlassMaterialObj = LoadObject<UMaterial>(nullptr, *(GlassPackageName + TEXT(".") + GlassMaterialName));
		UPackage* GlassPackage = nullptr;
		if (GlassMaterialObj)
		{
			UE_LOG(LogFragmentsUE, Log, TEXT("M_FragBase_Glass exists, updating it..."));
			GlassPackage = GlassMaterialObj->GetOutermost();
		}
		else
		{
			UE_LOG(LogFragmentsUE, Log, TEXT("Auto-generating glass material..."));
			GlassPackage = CreatePackage(*GlassPackageName);
			GlassMaterialObj = (UMaterial*)MaterialFactory->FactoryCreateNew(UMaterial::StaticClass(), GlassPackage, FName(*GlassMaterialName), RF_Public | RF_Standalone, nullptr, GWarn);
		}

		if (GlassMaterialObj)
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

			// Exact IFC color is roughly a dark tinted blue
			UMaterialExpressionVectorParameter* BaseColorExp = NewObject<UMaterialExpressionVectorParameter>(GlassMaterialObj);
			BaseColorExp->ParameterName = TEXT("BaseColor");
			BaseColorExp->DefaultValue = FLinearColor(0.05f, 0.15f, 0.35f, 1.0f); // IFC Blueish tint
			GlassMaterialObj->GetExpressionCollection().AddExpression(BaseColorExp);
			GlassMaterialObj->GetEditorOnlyData()->BaseColor.Expression = BaseColorExp;
			
			// Add a slight emissive glow so the blue tint is always visible and luminous
			UMaterialExpressionMultiply* EmissiveMul = NewObject<UMaterialExpressionMultiply>(GlassMaterialObj);
			EmissiveMul->A.Expression = BaseColorExp;
			EmissiveMul->ConstB = 0.35f; // 35% of base color as emissive
			GlassMaterialObj->GetExpressionCollection().AddExpression(EmissiveMul);
			GlassMaterialObj->GetEditorOnlyData()->EmissiveColor.Expression = EmissiveMul;

			UMaterialExpressionScalarParameter* OpacityExp = NewObject<UMaterialExpressionScalarParameter>(GlassMaterialObj);
			OpacityExp->ParameterName = TEXT("Opacity");
			OpacityExp->DefaultValue = 0.5f; // 50% transparent
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
			MetallicExp->DefaultValue = 0.0f; // Glass is dielectric
			GlassMaterialObj->GetExpressionCollection().AddExpression(MetallicExp);
			GlassMaterialObj->GetEditorOnlyData()->Metallic.Expression = MetallicExp;

			GlassMaterialObj->PostEditChange();
			FAssetRegistryModule::AssetCreated(GlassMaterialObj);
			
			FString GlassPackageFileName = FPackageName::LongPackageNameToFilename(GlassPackageName, FPackageName::GetAssetPackageExtension());
			FSavePackageArgs SaveArgs;
			SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
			SaveArgs.Error = GWarn;
			UPackage::SavePackage(GlassPackage, GlassMaterialObj, *GlassPackageFileName, SaveArgs);

			UE_LOG(LogFragmentsUE, Log, TEXT("Auto-generated M_FragBase_Glass successfully at %s"), *GlassPackageFileName);
	}
}

static FAutoConsoleCommand GCmdGenerateMaterial(
	TEXT("FragmentsUE.GenerateMaterial"),
	TEXT("Generates the default M_FragBase material in the plugin content folder"),
	FConsoleCommandDelegate::CreateStatic(&FFragmentsUEEditorModule::GenerateMaterials)
);

void FFragmentsUEEditorModule::StartupModule()
{
	UE_LOG(LogFragmentsUE, Log, TEXT("FragmentsUEEditor: Module loaded."));

	// Run material generation 3 seconds after module load so the editor and its validators are fully initialized
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([](float DeltaTime)
	{
		FFragmentsUEEditorModule::GenerateMaterials();
		return false; // Run once
	}), 3.0f);
}

void FFragmentsUEEditorModule::ShutdownModule()
{
}

IMPLEMENT_MODULE(FFragmentsUEEditorModule, FragmentsUEEditor)

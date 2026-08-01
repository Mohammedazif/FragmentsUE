using UnrealBuildTool;

public class FragmentsUEEditor : ModuleRules
{
	public FragmentsUEEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"FragmentsUE",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"UnrealEd",
			"AssetTools",
			"Slate",
			"SlateCore",
			"PropertyEditor",
			"InputCore",
			"ApplicationCore",
			"WorkspaceMenuStructure"
		});
	}
}

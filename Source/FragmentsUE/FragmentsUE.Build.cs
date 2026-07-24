using UnrealBuildTool;
using System.IO;

public class FragmentsUE : ModuleRules
{
	public FragmentsUE(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// Disable warnings from FlatBuffers generated code
		UndefinedIdentifierWarningLevel = WarningLevel.Off;

		// FlatBuffers header-only library + generated headers
		string ThirdPartyPath = Path.GetFullPath(Path.Combine(ModuleDirectory, "../../ThirdParty"));
		PublicIncludePaths.Add(Path.Combine(ThirdPartyPath, "flatbuffers/include"));
		PublicIncludePaths.Add(Path.Combine(ThirdPartyPath, "generated"));

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"MeshDescription",
			"StaticMeshDescription",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"ProceduralMeshComponent",
			"RenderCore",
			"zlib"
		});
	}
}

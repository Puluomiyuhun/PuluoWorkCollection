using UnrealBuildTool;

public class PaperFluidWindField : ModuleRules
{
    public PaperFluidWindField(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                "Core",
            }
        );

        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
                "CoreUObject",
                "Engine",
                "Slate",
                "SlateCore", 
                "Niagara",
                "Renderer",
                "RenderCore",
                "RHI",
                "Projects"
            }
        );
    }
}
// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class Test58 : ModuleRules
{
	public Test58(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"AIModule",
			"StateTreeModule",
			"GameplayStateTreeModule",
			"UMG",
			"Slate"
		});

		PrivateDependencyModuleNames.AddRange(new string[] { });

		PublicIncludePaths.AddRange(new string[] {
			"Test58",
			"Test58/Variant_Platforming",
			"Test58/Variant_Platforming/Animation",
			"Test58/Variant_Combat",
			"Test58/Variant_Combat/AI",
			"Test58/Variant_Combat/Animation",
			"Test58/Variant_Combat/Gameplay",
			"Test58/Variant_Combat/Interfaces",
			"Test58/Variant_Combat/UI",
			"Test58/Variant_SideScrolling",
			"Test58/Variant_SideScrolling/AI",
			"Test58/Variant_SideScrolling/Gameplay",
			"Test58/Variant_SideScrolling/Interfaces",
			"Test58/Variant_SideScrolling/UI"
		});

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}

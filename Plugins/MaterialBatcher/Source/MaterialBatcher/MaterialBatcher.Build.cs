// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class MaterialBatcher : ModuleRules
{
	public MaterialBatcher(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Slate",
			"SlateCore",
			"UnrealEd",          // 编辑器功能、资产创建
			"LevelEditor",       // 关卡编辑器菜单扩展
			"AssetTools",        // 资产工厂
			"AssetRegistry",
			"ToolMenus",         // 菜单注册
			"RenderCore",
			"RHI",
			"ImageCore",          // FImage / FImageView::CopyTo
			"MeshDescription",
			"StaticMeshDescription",
			"MaterialUtilities"
		});
	}
}

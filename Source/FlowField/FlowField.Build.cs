// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class FlowField : ModuleRules
{
	public FlowField(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",

				// Mass 框架
				"MassEntity",
				"MassRepresentation",
				"MassCommon",
				"MassSpawner",
				"MassLOD",
				"MassActors",
				"MassMovement",
				"MassNavigation",
				"MassReplication",

				// Mass AI
				"MassAIBehavior",

				// StateTree
				"StateTreeModule",
				"GameplayStateTreeModule",

				"GameplayTags",
				"NetCore",

				"MassSimulation", // ← 补上，这个最关键
				"StructUtils", // ← 补上
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				// 运行时安全模块
			}
		);

		// Editor 专用模块隔离，Shipping 不会包含
		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"UnrealEd",
					"LevelEditor",
					"ToolMenus",
					"EditorStyle",
					"Slate",
					"SlateCore"
				}
			);
		}

		SetupIrisSupport(Target);
	}
}
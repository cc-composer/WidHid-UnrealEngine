// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class WidHid : ModuleRules
{
	public WidHid(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput", "ImGui", "AkAudio", "WwiseSoundEngine" });
	}
}

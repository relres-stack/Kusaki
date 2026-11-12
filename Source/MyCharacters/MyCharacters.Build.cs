// Fill out your copyright notice in the Description page of Project Settings.

using UnrealBuildTool;

public class MyCharacters : ModuleRules
{
	public MyCharacters(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
	
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "AIModule", "NavigationSystem" });

		// UMG/Slate/SlateCore: for UCrosshairWidget, the fully-native (no Widget Blueprint asset needed)
		// screen-space HUD dot that replaced ABaseCharacter's old 3D world-space aim reticle meshes.
		// EnhancedInput: ABaseCharacter binds IA_Move/IA_Look/IA_MouseLook/IA_Jump and applies
		// IMC_Default/IMC_MouseLook directly in C++ (see BaseCharacter.cpp) instead of requiring each
		// Blueprint subclass to wire Enhanced Input nodes by hand.
		// CableComponent: UCableComponent, the visible chain between the Censer and the holder's hand/belt
		// socket — see AHolyItem::ChainVisualComponent/AttachChainToSocket().
		PrivateDependencyModuleNames.AddRange(new string[] { "Niagara", "UMG", "Slate", "SlateCore", "EnhancedInput", "CableComponent" });
		
		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}

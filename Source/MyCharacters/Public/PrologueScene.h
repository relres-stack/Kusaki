// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "PrologueScene.generated.h"

// How a staged scene is played (task 04, stage 3): APrologueDirector::PlayScene(), filled in by the "Play scene"
// action.
USTRUCT(BlueprintType)
struct MYCHARACTERS_API FPrologueSceneOptions
{
	GENERATED_BODY()

	// The player cannot move or turn the camera while it plays (and cannot aim).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scene")
	bool bBlockInput = true;

	// The HUD is hidden while it plays.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scene")
	bool bHideHud = false;

	// Monsters on their own AI this close to the player's hero (flat, cm) stop and wait while it plays. 0 = none.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scene")
	float HoldMonstersRadius = 3000.0f;

	// Control passes to this hero when it ends. Empty = no switch.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scene")
	FName SwitchControlAtEnd;

	// Seconds the view glides from its last shot to the hero the player controls then. 0 = a straight cut.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scene")
	float CameraBlendOut = 1.0f;

	// Seconds the view glides from the player's camera into its first shot. 0 = a straight cut. Done by the director,
	// so the scene itself needs no "Can Blend"/ease-in on its camera cuts (leave them off).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scene")
	float CameraBlendIn = 1.0f;

	// Step 3.2: before the scene starts, the player's hero walks by himself to the actor with this tag (input blocked)
	// and turns the way it faces — the scene needs him exactly there. None = the scene starts right away, wherever he is.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scene")
	FName PlayerMarkTag;

	// Seconds he may walk; not there by then = he is put on the mark.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scene")
	float MaxWalkTime = 6.0f;

	// Step 4.1: only the heroes the scene moves (bound by tag) stop and wait; the others go on with their own scripts.
	// For a far view where a hero keeps doing his part in the frame: in the binocular shot the Rider runs on along the
	// river with her own run animation, and the scene's camera only frames the river. Off: every hero waits.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scene")
	bool bHoldOnlyHeroesInScene = false;
};

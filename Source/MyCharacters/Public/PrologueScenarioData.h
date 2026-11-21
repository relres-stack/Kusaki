// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "PrologueScenarioData.generated.h"

class ABaseCharacter;

// One hero who takes part in the prologue.
USTRUCT(BlueprintType)
struct FPrologueHeroEntry
{
	GENERATED_BODY()

	// Name the scenario uses for this hero: "Redneck", "Priest", "Rider". Steps refer to heroes by this name,
	// never as "the player", so the same gate scene works whoever the player is controlling.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hero")
	FName HeroId;

	// Blueprint to spawn (BP_Redneck, BP_Priest, BP_Rider). A hero of this class that is already in the level
	// (for example the player's starting pawn) is used instead of spawning a second one.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hero")
	TSubclassOf<ABaseCharacter> HeroClass;
};

// Scenario data for APrologueDirector. Lives in an asset, not in the director's Details: the director is
// placed by rtd_build_level.py, and re-running that script resets every Details value of the actors it places.
// Stage 1 of task 04 holds only the hero list; steps, spawn rules, camera shots and monster roles are added in
// the next stages (see .claude/tasks/04-prolog-smena-geroev.md).
UCLASS(BlueprintType)
class MYCHARACTERS_API UPrologueScenarioData : public UDataAsset
{
	GENERATED_BODY()

public:
	// Every hero of the prologue, in the order the debug "next hero" key cycles through them.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Heroes")
	TArray<FPrologueHeroEntry> Heroes;

	// Hero the player controls when the prologue starts. Empty = whoever the GameMode gave the player.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Heroes")
	FName StartingHeroId;
};

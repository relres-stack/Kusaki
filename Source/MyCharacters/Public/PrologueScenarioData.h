// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "PrologueAppearance.h"
#include "PrologueScenarioData.generated.h"

class ABaseCharacter;
class UPrologueAction;
class UPrologueTrigger;

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

// How one hero enters the prologue (step 2.1): where he shows up, relative to what the player sees. The moment
// (trigger) is added with step 2.3; until then a hero appears when APrologueDirector::AppearHero() is called
// (debug key J, or later the scenario).
USTRUCT(BlueprintType)
struct FPrologueHeroAppearance
{
	GENERATED_BODY()

	// Which hero of Heroes this rule is for.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Appearance")
	FName HeroId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Appearance")
	FPrologueAppearancePlacement Placement;
};

// One step of the scenario (step 2.3): waits for its trigger, then runs its actions in order and the director
// moves on to the next step. Steps run one after another; things that go on in parallel (a hero walking on his
// own) are started by an action and keep running while later steps wait.
USTRUCT(BlueprintType)
struct FPrologueStep
{
	GENERATED_BODY()

	// For the debug readout and the log ("Redneck reaches the wasteland").
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Step")
	FString Name;

	// When the step fires. Empty = right away.
	UPROPERTY(EditAnywhere, Instanced, BlueprintReadOnly, Category = "Step")
	TObjectPtr<UPrologueTrigger> Trigger;

	// What happens when it fires, in order.
	UPROPERTY(EditAnywhere, Instanced, BlueprintReadOnly, Category = "Step")
	TArray<TObjectPtr<UPrologueAction>> Actions;
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

	// Where each hero shows up when the prologue brings him in (one rule per hero). A hero without a rule appears
	// with the default FPrologueAppearancePlacement values.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Appearance")
	TArray<FPrologueHeroAppearance> Appearances;

	// The scenario, step by step. Empty = the director only registers heroes (debug keys still work).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Steps")
	TArray<FPrologueStep> Steps;
};

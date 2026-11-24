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

	// Step 2.9: a checkpoint. Reaching it (the moment it starts waiting) remembers it; a failure later, or the player's
	// hero dying, reloads the level and the prologue goes on from here.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Checkpoint")
	bool bCheckpoint = false;

	// Run only when the prologue restarts from this checkpoint, before the step waits: put everyone where the step
	// expects them ("Place hero at marker" — it spawns a hero not in the level yet; switch control; monster roles;
	// knock someone down). Not run when the step is reached by playing.
	UPROPERTY(EditAnywhere, Instanced, BlueprintReadOnly, Category = "Checkpoint", meta = (EditCondition = "bCheckpoint"))
	TArray<TObjectPtr<UPrologueAction>> CheckpointSetup;

	// Step 2.9: while the step waits, this condition met long enough fails it (e.g. "Heroes too far apart" — the
	// Rider rode off and left the Priest without light). Empty = the step cannot fail.
	UPROPERTY(EditAnywhere, Instanced, BlueprintReadOnly, Category = "Failure")
	TObjectPtr<UPrologueTrigger> FailCondition;

	// Seconds the condition must hold before the step fails; meanwhile FailWarning is on screen. Back to normal in time
	// = no failure.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Failure", meta = (ClampMin = "0.0"))
	float FailAfter = 3.0f;

	// The warning while the condition holds (a stand-in for a voice line): "Don't leave the Priest behind!".
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Failure", meta = (MultiLine = "true"))
	FText FailWarning;

	// What happens when the step fails, in order — the scripted death ("Hero dies (scripted)", a line).
	UPROPERTY(EditAnywhere, Instanced, BlueprintReadOnly, Category = "Failure")
	TArray<TObjectPtr<UPrologueAction>> FailActions;

	// Seconds from the failure to the fade-out and the restart from the last checkpoint (time to see the death).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Failure", meta = (ClampMin = "0.0"))
	float RestartDelay = 3.0f;
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

	// Step 2.9: seconds of the fade to black before the level reloads for a restart, and of the fade back in after.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Checkpoint", meta = (ClampMin = "0.0"))
	float RestartFadeOut = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Checkpoint", meta = (ClampMin = "0.0"))
	float RestartFadeIn = 1.5f;
};

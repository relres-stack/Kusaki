// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "InputCoreTypes.h"
#include "PrologueDirector.generated.h"

class AAIController;
class ABaseCharacter;
class AIntroSceneDirector;
class APlayerController;
class UPrologueScenarioData;
struct FPrologueAppearancePlacement;
struct FPrologueHeroEntry;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPrologueHeroSwitched, FName, NewHeroId);

// Runs the prologue of the demo (Acts I-III): several heroes on one level, the player controls one of them at
// a time, and at the scenario's trigger points control passes to another hero. The heroes the player is not
// controlling act on their own under an AAIController.
//
// A plain AActor placed in the level, like AIntroSceneDirector (a scripted moment of the level, not a hero
// mechanic). It touches heroes only through public API. Starts after the opening scene (AIntroSceneDirector)
// is done, or right away if the level has none.
//
// Control model: ONE APlayerController for the whole game, and every hero has its own AAIController. Switching
// = the new hero's AI lets go, the player's controller possesses the new hero, the old hero gets its AI back.
// Monsters, HUD and input then follow the player's hero by the engine's own means: monsters only ever target
// GetPlayerCharacter(0), ABaseCharacter builds its HUD on possession and removes it in UnPossessed().
//
// Stage 1 of task 04 (.claude/tasks/04-prolog-smena-geroev.md): hero registry, spawning, switching, debug key.
// Scenario steps, camera shots and monster roles come in the next stages.
UCLASS()
class MYCHARACTERS_API APrologueDirector : public AActor
{
	GENERATED_BODY()

public:
	APrologueDirector();

	virtual void Tick(float DeltaSeconds) override;

	// Scenario data (hero list, later steps/shots/roles). Without it the director does nothing.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Prologue")
	TObjectPtr<UPrologueScenarioData> Scenario;

	// Untick to switch the prologue off (e.g. while testing other mechanics on this level).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Prologue")
	bool bRunOnBeginPlay = true;

	// Debug: this key passes control to the next hero of Scenario->Heroes (only heroes already in the level).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Prologue|Debug")
	bool bDebugSwitchKey = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Prologue|Debug")
	FKey DebugNextHeroKey = EKeys::N;

	// Debug: when the prologue starts, every hero of the list that is not in the level yet is spawned next to the
	// player's hero (a row to the right of him) and stands there under AI. Stage-1 test of the switching; the real
	// spawn rules (where and when each hero appears) replace it in stage 2.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Prologue|Debug")
	bool bDebugSpawnMissingHeroes = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Prologue|Debug", meta = (ClampMin = "100.0"))
	float DebugSpawnSpacing = 300.0f;

	// Debug: this key brings in the next hero of Scenario->Heroes that is not in the level yet, by his appearance
	// rule (AppearHero()). For testing step 2.1 switch bDebugSpawnMissingHeroes off, otherwise nobody is missing.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Prologue|Debug")
	bool bDebugAppearKey = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Prologue|Debug")
	FKey DebugAppearHeroKey = EKeys::J;

	// Debug: after AppearHero() the candidate points stay drawn this long (green = chosen, red = allowed but
	// hidden or worse, grey = outside the distance/angle limits). 0 = not drawn.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Prologue|Debug", meta = (ClampMin = "0.0"))
	float DebugDrawPointsTime = 8.0f;

	// Debug readout on screen (keys 600-609): who is controlled, which heroes exist.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Prologue|Debug")
	bool bShowDebug = true;

	// Passes control to the hero HeroId. Returns false (and logs why) if that hero does not exist, is dead, or
	// is already the one the player controls. Instant cut for now; the camera switch scene comes in stage 3.
	UFUNCTION(BlueprintCallable, Category = "Prologue")
	bool SwitchControlTo(FName HeroId);

	// Spawns hero HeroId at Where and puts it under AI. If that hero already exists, returns the existing one
	// and does not move it.
	UFUNCTION(BlueprintCallable, Category = "Prologue")
	ABaseCharacter* SpawnHero(FName HeroId, const FTransform& Where);

	// Brings hero HeroId into the prologue by his rule in Scenario->Appearances (step 2.1): spawns him under AI at
	// the best point the player can see right now — a candidate point with the rule's tag first, otherwise a point
	// around the player's view snapped to the navmesh. If no point is visible, the best one is used anyway (with a
	// warning): the scenario must go on. If the hero already exists, returns him and does not move him.
	UFUNCTION(BlueprintCallable, Category = "Prologue")
	ABaseCharacter* AppearHero(FName HeroId);

	UFUNCTION(BlueprintPure, Category = "Prologue")
	ABaseCharacter* GetHero(FName HeroId) const;

	// HeroId of the hero the player controls right now (None if it is not one of the prologue's heroes).
	UFUNCTION(BlueprintPure, Category = "Prologue")
	FName GetControlledHeroId() const;

	UFUNCTION(BlueprintPure, Category = "Prologue")
	bool HasStarted() const { return bStarted; }

	// Fired after control has passed to another hero.
	UPROPERTY(BlueprintAssignable, Category = "Prologue")
	FOnPrologueHeroSwitched OnHeroSwitched;

protected:
	virtual void BeginPlay() override;

private:
	// Waits for the player's pawn and for the opening scene to finish, then StartPrologue().
	void TryStart();
	void StartPrologue();

	// Registers every hero of the list already in the level (the player's pawn first).
	void RegisterExistingHeroes();
	void DebugSpawnMissingHeroes();

	const FPrologueHeroEntry* FindEntry(FName HeroId) const;

	// AppearHero()'s point search. Fills OutTransform (feet on the ground, facing) and a short description of where
	// it came from for the log. False only when not a single point could be found (no candidates, no navmesh).
	bool FindAppearanceTransform(const FPrologueAppearancePlacement& Placement, float CapsuleHalfHeight, FTransform& OutTransform, FString& OutWhere) const;
	FName FindHeroId(const ABaseCharacter* Hero) const;

	AAIController* GetOrCreateAIController(ABaseCharacter* Hero, FName HeroId);

	// The hero goes on under its AI: possessed by its AAIController, aiming suppressed. Monster damage/panic
	// immunity is deliberately NOT set here: an AI hero is already out of the monsters' reach (they only target
	// GetPlayerCharacter(0), and ABaseMonster::OnCapsuleHit() ignores every other hero), and the immunity flag is
	// shared with Moonshine (UMoonshineComponent turns it on and off during the buff), so touching it here would
	// fight that mechanic. Short immunity windows for the player's hero during scenes come with stage 3.
	void PutUnderAI(ABaseCharacter* Hero, FName HeroId);

	// The player takes the hero: aiming allowed again.
	void ReleaseFromAI(ABaseCharacter* Hero);

	void TickDebug();
	void ShowDebug(int32 Key, float Time, const FColor& Color, const FString& Message) const;

	UPROPERTY(Transient)
	TMap<FName, TObjectPtr<ABaseCharacter>> Heroes;

	UPROPERTY(Transient)
	TMap<FName, TObjectPtr<AAIController>> HeroAIControllers;

	UPROPERTY(Transient)
	TObjectPtr<AIntroSceneDirector> Intro;

	bool bStarted = false;
	int32 StartAttempts = 0;
	FTimerHandle StartRetryHandle;
};

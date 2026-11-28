// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Camera/CameraTypes.h"
#include "InputCoreTypes.h"
#include "PrologueMonsterRole.h"
#include "PrologueScene.h"
#include "PrologueDirector.generated.h"

class AAIController;
class ABaseCharacter;
class ABaseMonster;
class ALevelSequenceActor;
class ULevelSequence;
class ULevelSequencePlayer;
class AIntroSceneDirector;
class APlayerController;
class UHeroScriptRunner;
class UPrologueAction;
class UPrologueScenarioData;
class UScriptedKnockdownComponent;
struct FPrologueAppearancePlacement;
struct FPrologueHeroEntry;
struct FPrologueStep;

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
// Task 04 (.claude/tasks/04-prolog-smena-geroev.md). Done so far: hero registry, spawning, switching (stage 1);
// where a hero appears (AppearHero(), step 2.1); scenario steps = trigger + actions (Scenario->Steps, step 2.3,
// see UPrologueTrigger/UPrologueAction); heroes acting on their own (UHeroScriptRunner, step 2.5) and a hero's part
// that works whoever plays him (UPrologueTrigger_HeroTask, step 2.4). Monster roles and camera shots come next.
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

	// Debug: this key fires the current step right away, without waiting for its trigger.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Prologue|Debug")
	bool bDebugSkipStepKey = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Prologue|Debug")
	FKey DebugSkipStepKey = EKeys::K;

	// Debug (step 2.9): this key restarts the prologue from the last checkpoint (reloads the level).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Prologue|Debug")
	bool bDebugRestartKey = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Prologue|Debug")
	FKey DebugRestartKey = EKeys::L;

	// Debug (step 2.9, part of 3.6): start the prologue at this step (1 = the first one) as if it were a checkpoint —
	// its "Checkpoint Setup" runs, the steps before it do not. 0 = off. A checkpoint reached later still wins on a restart.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Prologue|Debug", meta = (ClampMin = "0"))
	int32 DebugStartStep = 0;

	// Debug: after AppearHero() the candidate points stay drawn this long (green = chosen, red = allowed but
	// hidden or worse, grey = outside the distance/angle limits). 0 = not drawn.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Prologue|Debug", meta = (ClampMin = "0.0"))
	float DebugDrawPointsTime = 8.0f;

	// AppearHero(): a point closer than this (flat, cm) to another hero or a monster counts as taken.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Prologue", meta = (ClampMin = "50.0"))
	float AppearanceFreeRadius = 150.0f;

	// Debug readout on screen (keys 600-609): who is controlled, which heroes exist, the current step, what each
	// hero's script is doing. Hero lines (subtitles of "Say a line") use keys 610+ and show regardless.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Prologue|Debug")
	bool bShowDebug = true;

	// Passes control to the hero HeroId. Returns false (and logs why) if that hero does not exist, is dead, or
	// is already the one the player controls. CameraBlend (step 4.6): seconds the view glides from what the player
	// saw to the new hero's camera; 0 = a straight cut. A hero leaving the player's control drops his screen-wide
	// effects first (the Priest's inflamed vision is turned off): they would stay on the new hero's screen.
	UFUNCTION(BlueprintCallable, Category = "Prologue")
	bool SwitchControlTo(FName HeroId, float CameraBlend = 0.0f);

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

	// Hero by name for triggers and actions: an empty HeroId means the hero the player controls right now.
	UFUNCTION(BlueprintPure, Category = "Prologue")
	ABaseCharacter* ResolveHero(FName HeroId) const;

	// Index into Scenario->Steps of the step waiting now; equal to the number of steps once they are all done,
	// INDEX_NONE before the prologue starts.
	UFUNCTION(BlueprintPure, Category = "Prologue")
	int32 GetCurrentStepIndex() const { return CurrentStepIndex; }

	// Seconds the current step has been waiting for its trigger (UPrologueTrigger_TimeElapsed).
	UFUNCTION(BlueprintPure, Category = "Prologue")
	float GetStepElapsedTime() const;

	// Goes up every time a step begins: tells one run of a step from another (a step run again after a restart
	// from a checkpoint must not see the hero script of its previous run as done).
	int32 GetStepSerial() const { return StepSerial; }

	// The hero's script runner (step 2.5), added to him the first time the scenario gives him something to do.
	UFUNCTION(BlueprintCallable, Category = "Prologue")
	UHeroScriptRunner* GetOrAddScriptRunner(ABaseCharacter* Hero);

	// The hero's scripted fall (step 2.6), added to him the first time the scenario knocks him down.
	UFUNCTION(BlueprintCallable, Category = "Prologue")
	UScriptedKnockdownComponent* GetOrAddKnockdown(ABaseCharacter* Hero);

	// Step 2.7: monsters cannot harm hero HeroId (no damage, panic, kill or touch) until this is called again with
	// false. Heroes under AI are protected regardless; this is for the player's hero (scenes, special moments).
	UFUNCTION(BlueprintCallable, Category = "Prologue")
	void SetHeroUntouchable(FName HeroId, bool bUntouchable);

	// Step 2.8. Picks the monsters of group GroupName by Selection; they replace its previous members (those not
	// picked again go back to their own AI), and a monster picked away from another group leaves that group. Returns
	// how many were picked. If the group already has a role, the new members take it at once.
	int32 SelectMonsterGroup(FName GroupName, const FPrologueMonsterSelection& Selection);

	// The monsters Selection picks, nearest first (living, not ridden; with Selection.bSkipOtherGroups, none from a group
	// other than ForGroup). Shared by SelectMonsterGroup() and "Place monsters at marker" (step 4.8).
	TArray<ABaseMonster*> FindMonsters(const FPrologueMonsterSelection& Selection, FName ForGroup) const;

	// Gives group GroupName a role: their own AI is switched off and the role drives them every frame. Null = they go
	// back to their own AI, the group keeps its members. Duration > 0: the role ends by itself after that many
	// seconds; bReleaseOnControlSwitch: it ends when control passes to another hero.
	void SetMonsterGroupRole(FName GroupName, UPrologueMonsterRole* NewRole, float Duration, bool bReleaseOnControlSwitch);

	// The group's living members (empty if there is no such group).
	UFUNCTION(BlueprintPure, Category = "Prologue|Monsters")
	TArray<ABaseMonster*> GetMonsterGroup(FName GroupName) const;

	// Name of the group the monster is in, None if in none.
	FName FindMonsterGroupOf(const ABaseMonster* Monster) const;

	// The monster is in group GroupName, or left it because a hero rides it (step 2.8c: a ridden monster leaves its
	// group at once, but "Hero rides a monster of a group" must still know where it came from).
	bool WasMonsterInGroup(const ABaseMonster* Monster, FName GroupName) const;

	// Largest light radius (cm) of the hero's lit held items; 0 = no light.
	float GetHeroLightRadius(const ABaseCharacter* Hero) const;

	// Goal moved out of every prologue hero's light (lit held item: its radius + the monster's StalkSafetyBuffer, a
	// candle only inside its cone), so a role monster holds at the light's edge instead of walking into it.
	FVector KeepOutOfHeroLight(const FVector& Goal, const ABaseMonster& Monster) const;

	// Stage 3 (steps 3.1, 3.3, basic 3.4): plays a staged scene — a Level Sequence shot on the level's own spots. Its
	// tracks are bound to the REAL heroes by tag: a binding tagged "Priest" in the sequence moves this level's Priest
	// (ALevelSequenceActor::SetBindingByTag), so after the scene every hero stays where the scene left him (Keep State)
	// and the game goes on from there. While it plays: the player's input and aim are blocked (Options.bBlockInput), heroes
	// are untouchable, their scripts wait, and monsters within HoldMonstersRadius of the player's hero that are on
	// their own AI stop and wait (they get their AI back afterwards). SwitchControlAtEnd: control passes to that hero
	// when the scene ends. CameraBlendOut (step 3.4): seconds the view glides from the scene's last shot to the camera of
	// the hero the player controls then (the new one after a switch) — no seam; 0 = a straight cut. PlayerMarkTag (step
	// 3.2): first the player's hero walks by himself to that mark and turns the way it faces, then the scene starts.
	// False if Scene is empty or another scene is still playing or being prepared.
	bool PlayScene(ULevelSequence* Scene, const FPrologueSceneOptions& Options);

	// The scene has played to its end (or was skipped) since the level started.
	bool HasSceneFinished(const ULevelSequence* Scene) const;

	// Step 4.5: seconds this scene has been playing (from its own start, not counting the player's walk to his mark).
	// Below 0 = it is not playing and has not finished; a very large number once it has finished (or was skipped).
	float GetSceneTime(const ULevelSequence* Scene) const;

	UFUNCTION(BlueprintPure, Category = "Prologue|Scene")
	bool IsScenePlaying() const { return bScenePlaying; }

	// Step 3.2: the player's hero is walking to his mark before a scene.
	bool IsScenePreparing() const { return bScenePreparing; }

	// Step 2.9: fades to black and reloads the level; the prologue then goes on from the last checkpoint reached
	// (UPrologueCheckpointSubsystem), or from the beginning if there is none.
	UFUNCTION(BlueprintCallable, Category = "Prologue")
	void RestartFromCheckpoint();

	// Fired after control has passed to another hero.
	UPROPERTY(BlueprintAssignable, Category = "Prologue")
	FOnPrologueHeroSwitched OnHeroSwitched;

protected:
	// Step 4.8: a prologue that starts at a later step (a restart from a checkpoint, or DebugStartStep) skips the level's
	// opening scene: its public bPlayOnBeginPlay is switched off here, before any actor's BeginPlay(). AIntroSceneDirector
	// itself is not changed.
	virtual void PostInitializeComponents() override;
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

	// The hero goes on under its AI: possessed by its AAIController, aiming suppressed, untouchable by monsters
	// (UpdateHeroProtection()). Moonshine's SetImmuneToMonsterAttacks() is deliberately not used: that flag belongs
	// to UMoonshineComponent, which turns it off itself when the buff ends.
	void PutUnderAI(ABaseCharacter* Hero, FName HeroId);

	// The player takes the hero: aiming allowed again, protection recomputed (UpdateHeroProtection()).
	void ReleaseFromAI(ABaseCharacter* Hero);

	// Step 2.7: ABaseCharacter::SetUntouchableByMonsters() for a hero = he is under AI, or lies after a scripted fall
	// that asked for it (UScriptedKnockdownComponent::IsUntouchableWhileDown()), or a step made him untouchable
	// (UntouchableHeroIds). The director owns this flag on the prologue's heroes: it is recomputed every frame and
	// written only when it changes.
	void UpdateHeroProtection(ABaseCharacter* Hero, FName HeroId);
	void UpdateAllHeroProtection();

	// Step 2.8: every frame, each group's role drives its members; dead and ridden members fall out, a role whose
	// time is up ends.
	void TickMonsterRoles();

	// The monster goes back to its own AI from wherever it stands (focus cleared, its speed for its state).
	void ReleaseMonster(ABaseMonster* Monster) const;

	// Undoes what a role set up on this member (the "Sit on hero" collision pass-through); called whenever a monster
	// leaves its role: role changed or ended, monster dropped, dead or ridden.
	void ClearMemberState(ABaseMonster* Monster, FPrologueRoleMemberState& State) const;

	// Ring places in the order the members stand around the role's hero (FPrologueRoleMemberState::SlotIndex).
	void AssignRoleSlots(FPrologueMonsterGroup& Group) const;

	void DrawMonsterGroupsDebug();

	// Runs the scenario steps: while the current step's trigger is met (or it has none), runs its actions and
	// moves on. At most one step per frame, so a chain of steps without triggers still reads in order in the log.
	void TickSteps();
	void BeginStep(int32 StepIndex);
	void FireCurrentStep(bool bForcedByDebug);

	// Runs a list of actions in order, skipping those whose OnlyIf is not met (logged with LogMark: "-" a step's
	// actions, "+" a checkpoint setup, "!" a failure).
	void RunActions(const TArray<TObjectPtr<UPrologueAction>>& Actions, const TCHAR* LogMark);

	// Step 2.9. Starts at StepIndex as a checkpoint: its CheckpointSetup runs, then the step waits. Why is for the log.
	void StartFromStep(int32 StepIndex, const TCHAR* Why);

	// The current step's failure condition: warning, then failure (FailActions), then the restart after RestartDelay.
	// True while the step has failed — it no longer goes on.
	bool TickFailCondition(const FPrologueStep& Step);

	void ReloadLevel();
	class UPrologueCheckpointSubsystem* GetCheckpoints() const;

	// Stage 3: the scene's player says it has finished (or was stopped); EndScene() gives the game back.
	UFUNCTION()
	void HandleSceneFinished();
	void EndScene(bool bSkipped);

	// The view glides from the shot From to the camera of the hero the player controls now: a temporary camera stands
	// exactly at From, then the engine blends from it to the hero (end of a scene, step 3.4; control switch, step 4.6).
	void GlideViewToPlayerHero(const FMinimalViewInfo& From, float Seconds, const TCHAR* Why);

	// The scene itself (after the player's hero is on his mark, or right away).
	bool StartScenePlayback(ULevelSequence* Scene);

	// Step 3.2: every frame while the player's hero walks to his mark and turns; then the scene starts. bPutOnMark: he
	// did not make it in time (or K) — he is put there.
	void TickSceneMark(float DeltaSeconds);
	void FinishSceneMark(bool bPutOnMark);

	// A line on screen for the player (the failure warning) — key 605, like the "Message" action; not debug-only.
	static void ShowSubtitle(const FString& Text, float Seconds);

	UFUNCTION()
	void HandleHeroScriptFinished(UHeroScriptRunner* Runner, bool bInterrupted);

	void TickDebug();
	void ShowDebug(int32 Key, float Time, const FColor& Color, const FString& Message) const;

	UPROPERTY(Transient)
	TMap<FName, TObjectPtr<ABaseCharacter>> Heroes;

	UPROPERTY(Transient)
	TMap<FName, TObjectPtr<AAIController>> HeroAIControllers;

	UPROPERTY(Transient)
	TObjectPtr<AIntroSceneDirector> Intro;

	// Heroes a step made untouchable (SetHeroUntouchable()).
	TSet<FName> UntouchableHeroIds;

	// Step 2.8: monster groups by name.
	UPROPERTY(Transient)
	TMap<FName, FPrologueMonsterGroup> MonsterGroups;

	// Monsters that left a group because a hero rides them, and that group (WasMonsterInGroup()).
	TMap<TWeakObjectPtr<ABaseMonster>, FName> RiddenAwayFrom;

	bool bStarted = false;
	int32 StartAttempts = 0;

	int32 CurrentStepIndex = INDEX_NONE;
	int32 StepSerial = 0;
	double StepStartTime = 0.0;
	FTimerHandle StartRetryHandle;

	// Step 2.9: the current step's failure state.
	enum class EFailPhase : uint8 { None, Warning, Failed };
	EFailPhase FailPhase = EFailPhase::None;
	double FailPhaseStartTime = 0.0;

	// Fading out before a reload (nothing else happens meanwhile).
	bool bRestarting = false;
	FTimerHandle RestartTimerHandle;

	// For the readout: the last checkpoint reached (index), INDEX_NONE if none.
	int32 LastCheckpointIndex = INDEX_NONE;

	// Stage 3: the scene playing now (its actor stays until the next scene so Keep State holds).
	UPROPERTY(Transient)
	TObjectPtr<ALevelSequenceActor> SceneActor;

	UPROPERTY(Transient)
	TObjectPtr<ULevelSequencePlayer> ScenePlayer;

	UPROPERTY(Transient)
	TObjectPtr<ULevelSequence> CurrentScene;

	bool bScenePlaying = false;
	FPrologueSceneOptions SceneOptions;

	// Step 3.2: walking the player's hero to his mark before the scene.
	bool bScenePreparing = false;
	bool bMarkTurning = false;
	UPROPERTY(Transient)
	TObjectPtr<ULevelSequence> PendingScene;
	TArray<FVector> MarkPath;
	int32 MarkPathIndex = 0;
	FVector MarkGoal = FVector::ZeroVector;
	float MarkYaw = 0.0f;
	double MarkStartTime = 0.0;

	// Step 3.4: what the camera showed on the scene's last frame (captured every frame while it plays), and the
	// temporary camera standing there while the view glides to the hero. The engine's own blend-out goes back to
	// the camera from BEFORE the scene (the old hero), which would seam a hero switch — hence done here.
	FMinimalViewInfo LastScenePOV;
	bool bHasScenePOV = false;

	// Glide into the scene: the player's view just before it started; waiting for the scene to take the camera.
	FMinimalViewInfo SceneStartPOV;
	bool bBlendInPending = false;
	double BlendInGiveUpTime = 0.0;

	UPROPERTY(Transient)
	TObjectPtr<class ACameraActor> SceneBlendCamera;

	FTimerHandle SceneBlendCleanupHandle;

	// Monsters the scene stopped (they were on their own AI) — they get it back when it ends.
	TArray<TWeakObjectPtr<ABaseMonster>> SceneHeldMonsters;

	// Scenes played to the end (or skipped) since the level started.
	TSet<FSoftObjectPath> FinishedScenes;
};

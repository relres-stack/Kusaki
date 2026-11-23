// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "HeroScriptRunner.generated.h"

class AAIController;
class ABaseCharacter;
class APrologueDirector;
class UHeroScriptCommand;
struct FHeroScriptTarget;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnHeroScriptFinished, UHeroScriptRunner*, Runner, bool, bInterrupted);

// Runs a hero's scripted actions in the prologue (task 04, step 2.5): walk/run/crawl somewhere, turn, wait, play a
// montage, say a line, teleport, hide, use an ability — one command after another (UHeroScriptCommand).
//
// APrologueDirector adds this component to a hero at runtime when the scenario first gives him something to do; it
// is not created in ABaseCharacter's constructor (rule 1: heroes are not changed for the scenario).
//
// Commands run only while the hero is under his AI controller. When the player takes the hero, the current command
// is cut short and the script waits; when the hero goes back to AI, that command starts again from its beginning.
// So a "waiting" behaviour (a looping script) simply goes on whenever the player is not controlling the hero.
UCLASS(ClassGroup = (Prologue), meta = (BlueprintSpawnableComponent))
class MYCHARACTERS_API UHeroScriptRunner : public UActorComponent
{
	GENERATED_BODY()

public:
	UHeroScriptRunner();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// Called by the director right after adding the component: who runs the scenario, the hero's scenario name,
	// and the on-screen message key his subtitles use.
	void Setup(APrologueDirector* InDirector, FName InHeroId, int32 InSubtitleKey);

	// Replaces whatever the hero is doing with Commands (templates from the scenario asset). Source and SourceSerial
	// say who started it — a step's trigger or action and the run of that step — so the step can later ask whether
	// ITS script is still going or done. An empty list just stops the current script.
	void RunScript(const TArray<TObjectPtr<UHeroScriptCommand>>& Commands, bool bInLoop, const UObject* InSource, int32 InSourceSerial);

	// Cuts the current script short (it counts as finished, interrupted).
	UFUNCTION(BlueprintCallable, Category = "Hero Script")
	void StopScript();

	// A script was started and is not finished yet (also while it waits for the player to let the hero go).
	UFUNCTION(BlueprintPure, Category = "Hero Script")
	bool IsBusy() const { return bHasScript && !bFinished; }

	// The script waits because the hero is not under his AI controller (the player controls him).
	UFUNCTION(BlueprintPure, Category = "Hero Script")
	bool IsPaused() const { return IsBusy() && bPaused; }

	bool IsRunning(const UObject* InSource, int32 InSourceSerial) const;
	bool HasFinished(const UObject* InSource, int32 InSourceSerial) const;

	// "2/3 walk to tag 'Gate' (paused: player controls him)", "done", "idle".
	FString DescribeState() const;

	// Draws what the running command is doing for one frame (the director's debug readout calls it).
	void DrawDebug() const;

	// Fired when a script ends: finished, or interrupted (replaced, stopped, hero died). Looping scripts end only by
	// interruption.
	UPROPERTY(BlueprintAssignable, Category = "Hero Script")
	FOnHeroScriptFinished OnScriptFinished;

	// --- For commands ---
	ABaseCharacter* GetHero() const;
	// The hero's controller if it is an AI one (null while the player controls him).
	AAIController* GetAIController() const;
	APrologueDirector* GetDirector() const { return Director.Get(); }
	FName GetHeroId() const { return HeroId; }
	FString GetHeroName() const;
	int32 GetSubtitleKey() const { return SubtitleKey; }
	double GetWorldTime() const;

	// Actor (may be null for a plain location) and point the target means right now. False if there is no such
	// actor (no marker with that tag, hero not in the level, no living monster).
	bool ResolveTarget(const FHeroScriptTarget& Target, AActor*& OutActor, FVector& OutLocation) const;

	// UHeroScriptCommand_Hide: hides the hero with everything he holds, or shows him again.
	void SetHeroHidden(bool bHidden);

	// The hero lies after a scripted fall or is still getting up (UScriptedKnockdownComponent, step 2.6): commands
	// that need him on his feet (UHeroScriptCommand::CanRunWhileDown() false) are skipped meanwhile.
	bool IsHeroDown() const;

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void StartCurrentCommand();
	void EndCurrentCommand(bool bInterrupted);
	void FinishScript(bool bInterrupted);

	// Templates from the scenario asset (never modified).
	UPROPERTY(Transient)
	TArray<TObjectPtr<UHeroScriptCommand>> Script;

	// Running copy of Script[CommandIndex].
	UPROPERTY(Transient)
	TObjectPtr<UHeroScriptCommand> Current;

	bool bLoop = false;
	bool bHasScript = false;
	bool bFinished = false;
	bool bPaused = false;
	bool bCommandStarted = false;
	int32 CommandIndex = INDEX_NONE;

	TWeakObjectPtr<const UObject> Source;
	int32 SourceSerial = INDEX_NONE;

	TWeakObjectPtr<APrologueDirector> Director;
	FName HeroId;
	int32 SubtitleKey = -1;

	bool bHiddenByScript = false;

	// A command skipped while the hero lies is logged once per fall, not every frame (a looping script of walks).
	bool bSkipWhileDownLogged = false;
	TArray<TWeakObjectPtr<AActor>> HiddenAttachedActors;
};

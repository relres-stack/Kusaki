// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "PrologueTrigger.generated.h"

class ABaseCharacter;
class APrologueDirector;
class UHeroScriptCommand;

// "When" of a prologue step (task 04, step 2.3): a condition APrologueDirector checks every frame while the step
// waits. Picked from a drop-down in the scenario asset (each kind shows only its own fields).
//
// Triggers live inside the scenario DataAsset, so they keep NO runtime state of their own (the same objects are
// reused by every PIE session); anything time-based reads the director (GetStepElapsedTime()). Heroes are named by
// HeroId ("Redneck", "Priest", "Rider"); an empty HeroId means "the hero the player controls right now".
UCLASS(Abstract, EditInlineNew, DefaultToInstanced, CollapseCategories)
class MYCHARACTERS_API UPrologueTrigger : public UObject
{
	GENERATED_BODY()

public:
	// Turns the condition around: "met while NOT ..." (eyes open again, hero left the zone, ...).
	UPROPERTY(EditAnywhere, Category = "Trigger")
	bool bInvert = false;

	bool IsMet(const APrologueDirector& Director) const { return Check(Director) != bInvert; }

	// Short text for the debug readout and the log, e.g. "Rider in zone 'Gate'".
	FString Describe() const { return (bInvert ? TEXT("NOT ") : TEXT("")) + DescribeCondition(); }

	// Draws what the trigger is waiting for in the world for one frame (zones), called by the director's debug
	// readout while this trigger's step waits. Nothing by default.
	virtual void DrawDebug(const APrologueDirector& Director) const {}

	// Every frame while this trigger's step waits, before IsMet(): a trigger that makes something happen (a hero
	// doing his part on his own, UPrologueTrigger_HeroTask) starts it here. Its state is kept by the hero's
	// UHeroScriptRunner, not by the trigger. Nothing by default.
	virtual void Update(APrologueDirector& Director) const {}

	// The step has fired (or was skipped): stop whatever Update() started and is still going.
	virtual void OnStepEnd(APrologueDirector& Director) const {}

protected:
	virtual bool Check(const APrologueDirector& Director) const PURE_VIRTUAL(UPrologueTrigger::Check, return false;);
	virtual FString DescribeCondition() const PURE_VIRTUAL(UPrologueTrigger::DescribeCondition, return FString(););

	// "Rider" or "player's hero" for an empty HeroId.
	static FString HeroName(FName HeroId);
};

// The hero is inside a zone: actors with ZoneTag placed in the level (a TriggerBox, or a TargetPoint with Radius).
UCLASS(meta = (DisplayName = "Hero in zone"))
class MYCHARACTERS_API UPrologueTrigger_HeroInZone : public UPrologueTrigger
{
	GENERATED_BODY()

public:
	// Whose position counts. Empty = the hero the player controls.
	UPROPERTY(EditAnywhere, Category = "Trigger")
	FName HeroId;

	// Every actor with this tag is a part of the zone (the hero has to be in any one of them).
	UPROPERTY(EditAnywhere, Category = "Trigger")
	FName ZoneTag;

	// Above 0: the hero is within this flat distance of a zone actor (height ignored), cm. 0: the hero overlaps
	// the zone actor (TriggerBox) or stands inside its bounding box.
	UPROPERTY(EditAnywhere, Category = "Trigger", meta = (ClampMin = "0.0"))
	float Radius = 0.0f;

	virtual void DrawDebug(const APrologueDirector& Director) const override;

protected:
	virtual bool Check(const APrologueDirector& Director) const override;
	virtual FString DescribeCondition() const override;
};

UENUM(BlueprintType)
enum class EPrologueHeroState : uint8
{
	EyesClosed			UMETA(DisplayName = "Eyes closed"),
	Trapped				UMETA(DisplayName = "In a bear trap"),
	Mounted				UMETA(DisplayName = "Riding a monster"),
	Panicking			UMETA(DisplayName = "Panicking"),
	Crouched			UMETA(DisplayName = "Crouched"),
	FingerFlame			UMETA(DisplayName = "Finger flame on (Priest)"),
	InflamedVision		UMETA(DisplayName = "Inflamed vision on (Priest)"),
	Dead				UMETA(DisplayName = "Dead"),
	// Scripted fall (step 2.6): lying, or still getting up. Inverted = he is fully on his feet again.
	KnockedDown			UMETA(DisplayName = "Lying after a scripted fall (or getting up)"),
};

// The hero is in a state right now (read from the hero, nothing is changed on him).
UCLASS(meta = (DisplayName = "Hero state"))
class MYCHARACTERS_API UPrologueTrigger_HeroState : public UPrologueTrigger
{
	GENERATED_BODY()

public:
	// Whose state counts. Empty = the hero the player controls.
	UPROPERTY(EditAnywhere, Category = "Trigger")
	FName HeroId;

	UPROPERTY(EditAnywhere, Category = "Trigger")
	EPrologueHeroState State = EPrologueHeroState::EyesClosed;

protected:
	virtual bool Check(const APrologueDirector& Director) const override;
	virtual FString DescribeCondition() const override;
};

// The step has been waiting this long.
UCLASS(meta = (DisplayName = "Time elapsed"))
class MYCHARACTERS_API UPrologueTrigger_TimeElapsed : public UPrologueTrigger
{
	GENERATED_BODY()

public:
	// Seconds since the step started waiting.
	UPROPERTY(EditAnywhere, Category = "Trigger", meta = (ClampMin = "0.0"))
	float Seconds = 3.0f;

protected:
	virtual bool Check(const APrologueDirector& Director) const override;
	virtual FString DescribeCondition() const override;
};

// No living monster near the hero ("the monsters have been driven off the Redneck").
UCLASS(meta = (DisplayName = "No monsters near hero"))
class MYCHARACTERS_API UPrologueTrigger_NoMonstersNear : public UPrologueTrigger
{
	GENERATED_BODY()

public:
	// Around whom. Empty = the hero the player controls.
	UPROPERTY(EditAnywhere, Category = "Trigger")
	FName HeroId;

	// cm. A monster closer than this (and alive) keeps the trigger from firing.
	UPROPERTY(EditAnywhere, Category = "Trigger", meta = (ClampMin = "0.0"))
	float Radius = 800.0f;

protected:
	virtual bool Check(const APrologueDirector& Director) const override;
	virtual FString DescribeCondition() const override;
};

// A staged scene ("Play scene") has played to its end, or was skipped (stage 3).
UCLASS(meta = (DisplayName = "Scene finished"))
class MYCHARACTERS_API UPrologueTrigger_SceneFinished : public UPrologueTrigger
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "Trigger")
	TObjectPtr<class ULevelSequence> Scene;

protected:
	virtual bool Check(const APrologueDirector& Director) const override;
	virtual FString DescribeCondition() const override;
};

// Two heroes are too far apart (step 2.9, a "Failure" condition): farther than MaxDistance, or — along the way to a
// goal marker — hero A is more than MaxAhead in front of hero B or more than MaxBehind behind him. Step 12b: the Rider
// must neither ride off ahead nor fall behind the Priest, or he is left without light.
UCLASS(meta = (DisplayName = "Heroes too far apart"))
class MYCHARACTERS_API UPrologueTrigger_HeroesApart : public UPrologueTrigger
{
	GENERATED_BODY()

public:
	// The one who must keep close (empty = the hero the player controls).
	UPROPERTY(EditAnywhere, Category = "Trigger")
	FName HeroA = TEXT("Rider");

	// The one A must stay with.
	UPROPERTY(EditAnywhere, Category = "Trigger")
	FName HeroB = TEXT("Priest");

	// Flat distance between them, cm. 0 = not checked.
	UPROPERTY(EditAnywhere, Category = "Trigger", meta = (ClampMin = "0.0"))
	float MaxDistance = 0.0f;

	// Where they are going: actors with this tag, the one nearest to B. None = no ahead/behind check.
	UPROPERTY(EditAnywhere, Category = "Trigger")
	FName GoalTag;

	// A is farther than this ahead of B toward the goal, cm. 0 = not checked.
	UPROPERTY(EditAnywhere, Category = "Trigger", meta = (ClampMin = "0.0"))
	float MaxAhead = 0.0f;

	// A is farther than this behind B, cm. 0 = not checked.
	UPROPERTY(EditAnywhere, Category = "Trigger", meta = (ClampMin = "0.0"))
	float MaxBehind = 0.0f;

	virtual void DrawDebug(const APrologueDirector& Director) const override;

protected:
	virtual bool Check(const APrologueDirector& Director) const override;
	virtual FString DescribeCondition() const override;

private:
	// How far A is ahead of B toward the goal (negative = behind). False if there is no goal or no hero.
	bool GetAlongGoal(const APrologueDirector& Director, float& OutAhead) const;
};

// The hero rides a monster of a group (step 2.8c): "the Rider has mounted the monster sitting on the Priest" (step 12).
// The monster leaves its group the moment it is ridden; the director remembers where it came from.
UCLASS(meta = (DisplayName = "Hero rides a monster of a group"))
class MYCHARACTERS_API UPrologueTrigger_HeroRidesGroupMonster : public UPrologueTrigger
{
	GENERATED_BODY()

public:
	// The rider. Empty = the hero the player controls.
	UPROPERTY(EditAnywhere, Category = "Trigger")
	FName HeroId = TEXT("Rider");

	// None = any monster.
	UPROPERTY(EditAnywhere, Category = "Trigger")
	FName GroupName = TEXT("Alpha");

protected:
	virtual bool Check(const APrologueDirector& Director) const override;
	virtual FString DescribeCondition() const override;
};

// At least MinCount living monsters of a group (step 2.8, "Monster group: role") are near the hero: "the pack has
// closed in". With Invert: fewer than MinCount — "the Priest has driven them off the Redneck".
UCLASS(meta = (DisplayName = "Monsters of a group near hero"))
class MYCHARACTERS_API UPrologueTrigger_MonsterGroupNear : public UPrologueTrigger
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "Trigger")
	FName GroupName = TEXT("Pack");

	// Around whom. Empty = the hero the player controls.
	UPROPERTY(EditAnywhere, Category = "Trigger")
	FName HeroId;

	// Flat distance, cm.
	UPROPERTY(EditAnywhere, Category = "Trigger", meta = (ClampMin = "0.0"))
	float Radius = 500.0f;

	UPROPERTY(EditAnywhere, Category = "Trigger", meta = (ClampMin = "1"))
	int32 MinCount = 1;

protected:
	virtual bool Check(const APrologueDirector& Director) const override;
	virtual FString DescribeCondition() const override;
};

// Every condition at once (e.g. "in the zone AND eyes closed"). Empty = met.
UCLASS(meta = (DisplayName = "All of"))
class MYCHARACTERS_API UPrologueTrigger_AllOf : public UPrologueTrigger
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Instanced, Category = "Trigger")
	TArray<TObjectPtr<UPrologueTrigger>> Conditions;

	virtual void DrawDebug(const APrologueDirector& Director) const override;
	virtual void Update(APrologueDirector& Director) const override;
	virtual void OnStepEnd(APrologueDirector& Director) const override;

protected:
	virtual bool Check(const APrologueDirector& Director) const override;
	virtual FString DescribeCondition() const override;
};

// Any one of the conditions. Empty = never met.
UCLASS(meta = (DisplayName = "Any of"))
class MYCHARACTERS_API UPrologueTrigger_AnyOf : public UPrologueTrigger
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Instanced, Category = "Trigger")
	TArray<TObjectPtr<UPrologueTrigger>> Conditions;

	virtual void DrawDebug(const APrologueDirector& Director) const override;
	virtual void Update(APrologueDirector& Director) const override;
	virtual void OnStepEnd(APrologueDirector& Director) const override;

protected:
	virtual bool Check(const APrologueDirector& Director) const override;
	virtual FString DescribeCondition() const override;
};

// A hero does his part of the scene, whoever plays him (step 2.4, "two modes"). If the player controls this hero, the
// step waits until the player does it himself (WhenPlayer). If not, the hero does it on his own (WhenAI, run by his
// UHeroScriptRunner) and the trigger is met when he is done. So one gate scene works for every start of the game:
// "the Priest reaches the Redneck" is the player walking there in one start and the Priest walking by script in
// another. If control changes while the step waits, the mode changes with it (the script waits / goes on).
// Use one such trigger per hero per step: two would keep replacing each other's script.
UCLASS(meta = (DisplayName = "Hero does it (player or script)"))
class MYCHARACTERS_API UPrologueTrigger_HeroTask : public UPrologueTrigger
{
	GENERATED_BODY()

public:
	// Whose part it is (a name, not "the player": the point is that it works either way).
	UPROPERTY(EditAnywhere, Category = "Trigger")
	FName HeroId;

	// While the player controls the hero: what the player has to do (e.g. "Priest in zone 'Redneck'"). Empty = met
	// right away.
	UPROPERTY(EditAnywhere, Instanced, Category = "Trigger")
	TObjectPtr<UPrologueTrigger> WhenPlayer;

	// While he is under AI: what he does on his own; met when the last command is done. Empty = met right away.
	UPROPERTY(EditAnywhere, Instanced, Category = "Trigger")
	TArray<TObjectPtr<UHeroScriptCommand>> WhenAI;

	virtual void DrawDebug(const APrologueDirector& Director) const override;
	virtual void Update(APrologueDirector& Director) const override;
	virtual void OnStepEnd(APrologueDirector& Director) const override;

protected:
	virtual bool Check(const APrologueDirector& Director) const override;
	virtual FString DescribeCondition() const override;
};

// The hero has finished the script an action gave him ("Hero script" action): nothing left to do. Never met for a
// looping script, or while his script waits because the player controls him.
UCLASS(meta = (DisplayName = "Hero finished his actions"))
class MYCHARACTERS_API UPrologueTrigger_HeroScriptDone : public UPrologueTrigger
{
	GENERATED_BODY()

public:
	// Empty = the hero the player controls.
	UPROPERTY(EditAnywhere, Category = "Trigger")
	FName HeroId;

protected:
	virtual bool Check(const APrologueDirector& Director) const override;
	virtual FString DescribeCondition() const override;
};

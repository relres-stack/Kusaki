// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "HeroScriptCommand.h"
#include "PrologueAction.generated.h"

class APrologueDirector;
class UAnimSequence;

// "What happens" when a prologue step fires (task 04): one thing the director does, picked from a drop-down in
// the scenario asset. Steps run their actions in order, all at once. Like UPrologueTrigger, actions live in the
// DataAsset and keep no runtime state of their own.
//
// Monster roles come with step 2.8, camera shots with stage 3.
UCLASS(Abstract, EditInlineNew, DefaultToInstanced, CollapseCategories)
class MYCHARACTERS_API UPrologueAction : public UObject
{
	GENERATED_BODY()

public:
	virtual void Execute(APrologueDirector& Director) const PURE_VIRTUAL(UPrologueAction::Execute, );

	// Short text for the log, e.g. "Priest appears".
	virtual FString Describe() const PURE_VIRTUAL(UPrologueAction::Describe, return FString(););
};

// A line of text on screen — a stand-in for subtitles/voice until those exist.
UCLASS(meta = (DisplayName = "Message (placeholder subtitle)"))
class MYCHARACTERS_API UPrologueAction_Message : public UPrologueAction
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "Action", meta = (MultiLine = "true"))
	FText Text;

	// Seconds on screen.
	UPROPERTY(EditAnywhere, Category = "Action", meta = (ClampMin = "0.5"))
	float Duration = 4.0f;

	virtual void Execute(APrologueDirector& Director) const override;
	virtual FString Describe() const override;
};

// A hero enters the prologue where the player can see him (APrologueDirector::AppearHero(), his rule in
// Scenario->Appearances). Nothing happens if he is already in the level.
UCLASS(meta = (DisplayName = "Hero appears"))
class MYCHARACTERS_API UPrologueAction_AppearHero : public UPrologueAction
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "Action")
	FName HeroId;

	virtual void Execute(APrologueDirector& Director) const override;
	virtual FString Describe() const override;
};

// Control passes to a hero (instant cut for now; the camera switch scene comes with stage 3).
UCLASS(meta = (DisplayName = "Switch control to hero"))
class MYCHARACTERS_API UPrologueAction_SwitchControl : public UPrologueAction
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "Action")
	FName HeroId;

	virtual void Execute(APrologueDirector& Director) const override;
	virtual FString Describe() const override;
};

// Gives a hero something to do on his own (step 2.5): walk/run/crawl, turn, wait, montage, a line, teleport, hide,
// use an ability — UHeroScriptRunner runs it while the hero is under AI, in parallel with the following steps.
// Replaces what he was doing; an empty list just stops him. With "Loop" on it is a "waiting" behaviour that goes on
// until another script replaces it. A step can wait for it with the "Hero finished his actions" trigger.
// For something the hero must do whoever plays him, use the "Hero does it (player or script)" trigger instead.
UCLASS(meta = (DisplayName = "Hero script (on his own)"))
class MYCHARACTERS_API UPrologueAction_HeroScript : public UPrologueAction
{
	GENERATED_BODY()

public:
	// Whose script. Empty = the hero the player controls (it then waits until the hero is under AI again).
	UPROPERTY(EditAnywhere, Category = "Action")
	FName HeroId;

	UPROPERTY(EditAnywhere, Category = "Action")
	FHeroScript Script;

	virtual void Execute(APrologueDirector& Director) const override;
	virtual FString Describe() const override;
};

// Scripted fall (step 2.6): the hero falls without damage or panic and lies until a later step makes him get up
// (UScriptedKnockdownComponent). Works for the player's hero too: he cannot walk, but can look around and use his
// abilities (step 11: the Priest lies and turns on his inflamed vision).
UCLASS(meta = (DisplayName = "Knock hero down / get up"))
class MYCHARACTERS_API UPrologueAction_KnockDown : public UPrologueAction
{
	GENERATED_BODY()

public:
	// Who. Empty = the hero the player controls.
	UPROPERTY(EditAnywhere, Category = "Action")
	FName HeroId;

	// Off: he falls. On: he gets up.
	UPROPERTY(EditAnywhere, Category = "Action")
	bool bGetUp = false;

	// Fall or get-up animation, only for this moment (its last frame is the lying pose). Empty = the hero's own
	// KnockdownFallAnimation / GetUpAnimation.
	UPROPERTY(EditAnywhere, Category = "Action")
	TObjectPtr<UAnimSequence> Animation;

	// When he falls, his own script stops (he cannot walk lying). Give him a new one for lines while lying.
	UPROPERTY(EditAnywhere, Category = "Action", meta = (EditCondition = "!bGetUp"))
	bool bStopHisScript = true;

	virtual void Execute(APrologueDirector& Director) const override;
	virtual FString Describe() const override;
};

// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "PrologueAction.generated.h"

class APrologueDirector;

// "What happens" when a prologue step fires (task 04): one thing the director does, picked from a drop-down in
// the scenario asset. Steps run their actions in order, all at once. Like UPrologueTrigger, actions live in the
// DataAsset and keep no runtime state of their own.
//
// Stage 2.3 brings only the basic ones; hero script actions (walk, crawl, say a line...) come with step 2.5,
// monster roles with 2.8, camera shots with stage 3.
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

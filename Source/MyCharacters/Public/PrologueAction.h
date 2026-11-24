// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "HeroScriptCommand.h"
#include "HolyItem.h"
#include "PrologueMonsterRole.h"
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

	// Monsters cannot harm him (no damage, panic, kill or touch) until he is fully up again (step 2.7) — the Priest
	// lying in the crowd. Off = ordinary rules for the player's hero while he lies (a hero under AI is protected anyway).
	UPROPERTY(EditAnywhere, Category = "Action", meta = (EditCondition = "!bGetUp"))
	bool bUntouchableWhileDown = true;

	virtual void Execute(APrologueDirector& Director) const override;
	virtual FString Describe() const override;
};

// Monsters cannot harm this hero (step 2.7): no damage, panic, kill or touch, they do not even swing at him; they still
// see, chase and surround him. For scenes and other moments of the player's hero; heroes the player does not control
// are protected anyway. Stays until a later step turns it off (also across control switches).
UCLASS(meta = (DisplayName = "Hero untouchable by monsters on/off"))
class MYCHARACTERS_API UPrologueAction_HeroUntouchable : public UPrologueAction
{
	GENERATED_BODY()

public:
	// Who. Empty = the hero the player controls at the moment the step fires.
	UPROPERTY(EditAnywhere, Category = "Action")
	FName HeroId;

	// On: protected from now on. Off: ordinary rules again (unless he is under AI or lying after a protected fall).
	UPROPERTY(EditAnywhere, Category = "Action")
	bool bUntouchable = true;

	virtual void Execute(APrologueDirector& Director) const override;
	virtual FString Describe() const override;
};

// A scripted death (step 2.9, usually in a step's "Fail Actions"): the hero dies right away (ABaseCharacter::
// HandleDeath() — the usual ragdoll), whatever protects him (untouchable, lying after a scripted fall). His own script
// stops. Step 12b: the Priest, left without light, dies; then the prologue restarts from the last checkpoint.
UCLASS(meta = (DisplayName = "Hero dies (scripted)"))
class MYCHARACTERS_API UPrologueAction_KillHero : public UPrologueAction
{
	GENERATED_BODY()

public:
	// Who. Empty = the hero the player controls.
	UPROPERTY(EditAnywhere, Category = "Action")
	FName HeroId;

	virtual void Execute(APrologueDirector& Director) const override;
	virtual FString Describe() const override;
};

// Puts a hero on a marker placed in the level (step 2.9, mostly in a checkpoint's "Checkpoint Setup"): an actor with
// MarkerTag (a TargetPoint). A hero not in the level yet is spawned there under AI. Whatever he was walking to is
// dropped (his script goes on with its next command).
UCLASS(meta = (DisplayName = "Place hero at marker"))
class MYCHARACTERS_API UPrologueAction_PlaceHero : public UPrologueAction
{
	GENERATED_BODY()

public:
	// Who. Empty = the hero the player controls.
	UPROPERTY(EditAnywhere, Category = "Action")
	FName HeroId;

	// The marker: the first actor found with this tag.
	UPROPERTY(EditAnywhere, Category = "Action")
	FName MarkerTag;

	// He faces the way the marker faces. Off: he keeps his own facing.
	UPROPERTY(EditAnywhere, Category = "Action")
	bool bUseMarkerRotation = true;

	virtual void Execute(APrologueDirector& Director) const override;
	virtual FString Describe() const override;
};

// A holy item straight into a hero's free hand (steps 2.10-2.11, and checkpoint setups: after a restart the Priest
// must hold his censer again). Through the hero's own ABaseCharacter::PickUp(), as if he had picked it up; nothing
// happens if both his hands are full.
UCLASS(meta = (DisplayName = "Give hero an item"))
class MYCHARACTERS_API UPrologueAction_GiveItem : public UPrologueAction
{
	GENERATED_BODY()

public:
	// Who. Empty = the hero the player controls.
	UPROPERTY(EditAnywhere, Category = "Action")
	FName HeroId;

	// BP_Censer, BP_Candle...
	UPROPERTY(EditAnywhere, Category = "Action")
	TSubclassOf<AHolyItem> ItemClass;

	// Oil / wax it has (clamped to its MaxFuel). Below 0 = the item's own default.
	UPROPERTY(EditAnywhere, Category = "Action")
	float Fuel = -1.0f;

	// Lit in his hand (if it has fuel). Off: he holds it unlit.
	UPROPERTY(EditAnywhere, Category = "Action")
	bool bLit = true;

	virtual void Execute(APrologueDirector& Director) const override;
	virtual FString Describe() const override;
};

// The item passes from one hero's hand to another's (step 2.10: the Priest gives the Rider his censer, scene 12a). The
// hand-over itself is a staged scene; this is what happens in the game at its end: the giver's item is gone, the
// taker holds the same kind of item with the same fuel, lit if it was lit (she swings a censer over her head as soon
// as she holds one). The giver's hand is empty afterwards (the hero's code treats a removed item as an empty hand), so
// no new function on the hero is needed.
UCLASS(meta = (DisplayName = "Hand an item over (scene stand-in)"))
class MYCHARACTERS_API UPrologueAction_HandOverItem : public UPrologueAction
{
	GENERATED_BODY()

public:
	// The giver (empty = the hero the player controls).
	UPROPERTY(EditAnywhere, Category = "Action")
	FName FromHeroId = TEXT("Priest");

	// The taker.
	UPROPERTY(EditAnywhere, Category = "Action")
	FName ToHeroId = TEXT("Rider");

	// Which of the giver's items (his right hand first).
	UPROPERTY(EditAnywhere, Category = "Action")
	EHolyItemType ItemType = EHolyItemType::Censer;

	virtual void Execute(APrologueDirector& Director) const override;
	virtual FString Describe() const override;
};

// Lights or puts out a hero's held item (step 2.11: the Priest gets up and lights his censer with little oil left).
// Through AHolyItem::LightUp() / Extinguish(); an item with no fuel does not light (LightUp()'s own rule).
UCLASS(meta = (DisplayName = "Light / put out a hero's item"))
class MYCHARACTERS_API UPrologueAction_LightItem : public UPrologueAction
{
	GENERATED_BODY()

public:
	// Who. Empty = the hero the player controls.
	UPROPERTY(EditAnywhere, Category = "Action")
	FName HeroId;

	// Which of his items: every held item of this type.
	UPROPERTY(EditAnywhere, Category = "Action")
	EHolyItemType ItemType = EHolyItemType::Censer;

	// On: light it. Off: put it out.
	UPROPERTY(EditAnywhere, Category = "Action")
	bool bLight = true;

	// Before lighting, set its fuel to this (clamped to MaxFuel). Below 0 = keep what it has.
	UPROPERTY(EditAnywhere, Category = "Action")
	float Fuel = -1.0f;

	virtual void Execute(APrologueDirector& Director) const override;
	virtual FString Describe() const override;
};

// What "Monster group: do once" does (step 2.8b).
UENUM(BlueprintType)
enum class EPrologueMonsterGroupDo : uint8
{
	// Thrown back away from the hero (ABaseMonster::ApplyKnockback) — "bounces off the candle".
	KnockBack	UMETA(DisplayName = "Bounce off the hero (knockback)"),
	// Stands stunned for StunDuration (ABaseMonster::Stun).
	Stun		UMETA(DisplayName = "Stun"),
	// Plays its attack swing, nobody is hit (ABaseMonster::PlayAttackAnimation).
	Swing		UMETA(DisplayName = "Swing (no hit)"),
	// The nearest chosen monster calls the pack (ABaseMonster::BroadcastAlert): monsters on their own AI within
	// its alert radius of the spot come running. Monsters in a role do not answer.
	CallOthers	UMETA(DisplayName = "Call the others (pack alert)"),
};

// Something done once to monsters of a group (step 2.8b), on top of whatever role it has: bounce them off a hero,
// stun them, make one swing, make one call the others. "Who" is counted from the hero, nearest first.
UCLASS(meta = (DisplayName = "Monster group: do once"))
class MYCHARACTERS_API UPrologueAction_MonsterGroupDo : public UPrologueAction
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "Action")
	FName GroupName = TEXT("Pack");

	UPROPERTY(EditAnywhere, Category = "Action")
	EPrologueMonsterGroupDo What = EPrologueMonsterGroupDo::KnockBack;

	// From whom: knockback goes away from him, the call is made to his spot, the monsters are counted from him.
	// Empty = the hero the player controls.
	UPROPERTY(EditAnywhere, Category = "Action")
	FName HeroId;

	// Only members this close to the hero (flat, cm). 0 = any distance.
	UPROPERTY(EditAnywhere, Category = "Action", meta = (ClampMin = "0.0"))
	float Radius = 0.0f;

	// At most this many, the nearest to the hero first. 0 = all that are close enough.
	UPROPERTY(EditAnywhere, Category = "Action", meta = (ClampMin = "0"))
	int32 MaxCount = 0;

	UPROPERTY(EditAnywhere, Category = "Action", meta = (EditCondition = "What == EPrologueMonsterGroupDo::KnockBack", EditConditionHides, ClampMin = "0.0"))
	float PushForce = 800.0f;

	UPROPERTY(EditAnywhere, Category = "Action", meta = (EditCondition = "What == EPrologueMonsterGroupDo::KnockBack", EditConditionHides, ClampMin = "0.0"))
	float UpwardForce = 150.0f;

	// Seconds.
	UPROPERTY(EditAnywhere, Category = "Action", meta = (EditCondition = "What == EPrologueMonsterGroupDo::Stun", EditConditionHides, ClampMin = "0.1"))
	float StunDuration = 3.0f;

	// On: the pack is called to the hero's spot. Off: to where the calling monster stands.
	UPROPERTY(EditAnywhere, Category = "Action", meta = (EditCondition = "What == EPrologueMonsterGroupDo::CallOthers", EditConditionHides))
	bool bCallToHero = true;

	virtual void Execute(APrologueDirector& Director) const override;
	virtual FString Describe() const override;
};

// A named group of monsters gets a scripted role (step 2.8): surround a hero, run to him, ... Their own AI is off while
// the role lasts. Later steps refer to the same group by its name (give it another role, wait for "Monsters of a group
// near hero"). An empty role sends the group back to its own AI.
UCLASS(meta = (DisplayName = "Monster group: role"))
class MYCHARACTERS_API UPrologueAction_MonsterRole : public UPrologueAction
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "Action")
	FName GroupName = TEXT("Pack");

	// On: pick the group's monsters now by Selection (its previous members go back to their own AI). Off: the same
	// monsters as before get the new role.
	UPROPERTY(EditAnywhere, Category = "Action")
	bool bSelectMonsters = true;

	UPROPERTY(EditAnywhere, Category = "Action", meta = (EditCondition = "bSelectMonsters"))
	FPrologueMonsterSelection Selection;

	// Empty = back to their own AI (the group keeps its monsters for a later role).
	UPROPERTY(EditAnywhere, Instanced, Category = "Action")
	TObjectPtr<UPrologueMonsterRole> Role;

	// Seconds the role lasts, then their own AI again. 0 = until another step changes it.
	UPROPERTY(EditAnywhere, Category = "Action", meta = (ClampMin = "0.0"))
	float Duration = 0.0f;

	// The role ends when control passes to another hero.
	UPROPERTY(EditAnywhere, Category = "Action")
	bool bReleaseOnControlSwitch = false;

	virtual void Execute(APrologueDirector& Director) const override;
	virtual FString Describe() const override;
};

// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "PrologueMonsterRole.generated.h"

class ABaseCharacter;
class ABaseMonster;
class APrologueDirector;

// Which monsters make up a group (task 04, step 2.8). Nearest to the hero first.
USTRUCT(BlueprintType)
struct MYCHARACTERS_API FPrologueMonsterSelection
{
	GENERATED_BODY()

	// Only monsters with this actor tag. None = any monster.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Selection")
	FName MonsterTag;

	// Counted from this hero, nearest first. Empty = the hero the player controls.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Selection")
	FName NearHeroId;

	// Flat distance from that hero, cm. 0 = anywhere in the level.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Selection", meta = (ClampMin = "0.0"))
	float Radius = 2500.0f;

	// At most this many (the nearest ones). 0 = every monster that matches.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Selection", meta = (ClampMin = "0"))
	int32 MaxCount = 0;

	// A monster already in another group is left there.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Selection")
	bool bSkipOtherGroups = true;
};

// What one monster of a group is doing under its role — kept by the director, because roles live in the scenario
// DataAsset and keep no runtime state (like triggers and actions).
USTRUCT()
struct MYCHARACTERS_API FPrologueRoleMemberState
{
	GENERATED_BODY()

	// Place in the ring ("Surround hero"): slot SlotIndex of SlotCount, starting at SlotBaseYaw. Set when the role is
	// given, in the order the monsters stand around the hero, so nobody has to cross the ring.
	int32 SlotIndex = 0;
	int32 SlotCount = 1;
	float SlotBaseYaw = 0.0f;

	// Random per monster: the pressing in and out of the ring is not in step.
	float Phase = 0.0f;

	double NextSwingTime = 0.0;
	double NextMoveTime = 0.0;
	FVector LastGoal = FVector::ZeroVector;
	bool bHasGoal = false;

	// "Driven off" (UPrologueMonsterRole::bRetreatWhenHit): hit by light while it lasts; once the stun is over the
	// monster backs off until RetreatUntil.
	bool bRetreatPending = false;
	double RetreatUntil = 0.0;

	bool IsRetreating(double Time) const { return Time < RetreatUntil; }

	// "Sit on hero": the hero this monster and he ignore each other's collision with (so it can stand right on top of
	// him). Undone by the director whenever the monster leaves the role (APrologueDirector::ClearMemberState()).
	TWeakObjectPtr<ABaseCharacter> PassThroughHero;
};

// Everything a role needs for one monster this frame.
struct FPrologueRoleContext
{
	APrologueDirector* Director = nullptr;
	ABaseMonster* Monster = nullptr;
	// The hero the role is about (null = not in the level: the monster just stands).
	ABaseCharacter* Hero = nullptr;
	double Time = 0.0;
};

// A scripted role of a monster group (task 04, step 2.8): what the monsters do around a hero while the scenario
// drives them. APrologueDirector gives it to a group ("Monster group: role" action), switches the monsters' own AI
// off for that time (ABaseMonster::SetScriptControlled()) and runs the role every frame through the monster's public
// API only (RequestMoveToLocation, SetFocus, PlayAttackAnimation).
//
// Common to every role: the monsters never step into the light of a hero's lit item (for a candle only inside its
// cone), so they hold at its edge instead of being stunned; a stunned, staggered or sun-fleeing monster is left alone
// until that is over. Picked from a drop-down in the scenario asset, keeps no runtime state (FPrologueRoleMemberState).
UCLASS(Abstract, EditInlineNew, DefaultToInstanced, CollapseCategories)
class MYCHARACTERS_API UPrologueMonsterRole : public UObject
{
	GENERATED_BODY()

public:
	// Around whom. Empty = the hero the player controls, read again every frame (after a control switch the role
	// follows the new hero) — name the hero to stay on him.
	UPROPERTY(EditAnywhere, Category = "Role")
	FName HeroId;

	// Run (chase speed) or walk (patrol speed).
	UPROPERTY(EditAnywhere, Category = "Role")
	bool bRun = true;

	// Hold at the edge of any lit item of any hero instead of walking into it. Off: they walk in and get stunned
	// by it ("bounce off the candle") — together with "Retreat when hit" that is "the Priest drives them off".
	// Holy light zones placed in the level (AHolyLightZone, deadly) are kept out of either way.
	UPROPERTY(EditAnywhere, Category = "Role")
	bool bKeepOutOfLight = true;

	// A monster stunned or knocked back (a candle, a censer) is "driven off": once it can move again it backs off to
	// RetreatDistance from the hero for RetreatDuration seconds, then goes back to its role. Step 7: the Priest drives
	// the pack off the Redneck by turning his shield to each of them, and they keep coming back.
	UPROPERTY(EditAnywhere, Category = "Role|Driven off")
	bool bRetreatWhenHit = false;

	// How far from the hero it backs off (flat, cm).
	UPROPERTY(EditAnywhere, Category = "Role|Driven off", meta = (EditCondition = "bRetreatWhenHit", ClampMin = "100.0"))
	float RetreatDistance = 1000.0f;

	// Seconds it stays off before coming back. 0 = it does not come back while this role lasts.
	UPROPERTY(EditAnywhere, Category = "Role|Driven off", meta = (EditCondition = "bRetreatWhenHit", ClampMin = "0.0"))
	float RetreatDuration = 8.0f;

	// Swing at the hero now and then without hitting (only the animation; the hero is protected anyway, step 2.7).
	UPROPERTY(EditAnywhere, Category = "Role|Swings")
	bool bFakeSwings = false;

	// Only when this close to the hero (flat, cm).
	UPROPERTY(EditAnywhere, Category = "Role|Swings", meta = (EditCondition = "bFakeSwings", ClampMin = "0.0"))
	float SwingDistance = 350.0f;

	// Seconds between swings of one monster, random in this range.
	UPROPERTY(EditAnywhere, Category = "Role|Swings", meta = (EditCondition = "bFakeSwings", ClampMin = "0.3"))
	float SwingIntervalMin = 2.0f;

	UPROPERTY(EditAnywhere, Category = "Role|Swings", meta = (EditCondition = "bFakeSwings", ClampMin = "0.3"))
	float SwingIntervalMax = 5.0f;

	// One frame of the role for one monster: where to go, face the hero, swing.
	void TickMember(const FPrologueRoleContext& Context, FPrologueRoleMemberState& State) const;

	// "surround Redneck at 3.5 m" — for the log and the debug readout.
	virtual FString Describe() const PURE_VIRTUAL(UPrologueMonsterRole::Describe, return FString(););

protected:
	// Where this monster should stand now, and how close counts as there. False = stay where it is.
	virtual bool GetGoal(const FPrologueRoleContext& Context, const FPrologueRoleMemberState& State, FVector& OutGoal, float& OutAcceptance) const
		PURE_VIRTUAL(UPrologueMonsterRole::GetGoal, return false;);

	// Every frame before the goal, while the hero is in the level (also while the monster is stunned or hiding from
	// the sun): a role's own setup that must hold the whole time. Nothing by default.
	virtual void UpdateMember(const FPrologueRoleContext& Context, FPrologueRoleMemberState& State) const {}

	// "Redneck" or "player's hero".
	FString HeroName() const;
};

// Run up to the hero and stop a little short of him, facing him.
UCLASS(meta = (DisplayName = "Run to hero"))
class MYCHARACTERS_API UPrologueMonsterRole_GoToHero : public UPrologueMonsterRole
{
	GENERATED_BODY()

public:
	// Stops this far from him (flat, cm).
	UPROPERTY(EditAnywhere, Category = "Role", meta = (ClampMin = "50.0"))
	float StopDistance = 250.0f;

	virtual FString Describe() const override;

protected:
	virtual bool GetGoal(const FPrologueRoleContext& Context, const FPrologueRoleMemberState& State, FVector& OutGoal, float& OutAcceptance) const override;
};

// Stand in a ring around the hero, facing him, stepping in and out ("pressing"); the ring follows him when he moves.
// With "Radius from his light" it is the ring at the edge of his own light — "hold at the edge of the light".
UCLASS(meta = (DisplayName = "Surround hero"))
class MYCHARACTERS_API UPrologueMonsterRole_Surround : public UPrologueMonsterRole
{
	GENERATED_BODY()

public:
	// Ring radius (flat, cm).
	UPROPERTY(EditAnywhere, Category = "Role", meta = (ClampMin = "50.0"))
	float Radius = 350.0f;

	// While he holds a lit item, the ring is at the edge of its light instead (its radius + the monster's
	// StalkSafetyBuffer); Radius when he has no light.
	UPROPERTY(EditAnywhere, Category = "Role")
	bool bRadiusFromHisLight = false;

	// How far the monsters step in from the ring and back (cm). 0 = they stand still on it.
	UPROPERTY(EditAnywhere, Category = "Role", meta = (ClampMin = "0.0"))
	float PressDepth = 80.0f;

	// Seconds for one step in and back out.
	UPROPERTY(EditAnywhere, Category = "Role", meta = (ClampMin = "0.5"))
	float PressPeriod = 4.0f;

	virtual FString Describe() const override;

protected:
	virtual bool GetGoal(const FPrologueRoleContext& Context, const FPrologueRoleMemberState& State, FVector& OutGoal, float& OutAcceptance) const override;
};

// The most dangerous monster sits on a hero lying on the ground (step 10: on the Priest, until the Rider mounts it and
// drags it off, step 12). It stands right on top of him, facing him: the two ignore each other's collision while the
// role lasts, and swings at him now and then if "Fake swings" is on. A real "sitting" pose needs its own animation
// (outside this task). Give it to a group of one ("Monster group: role", Max Count 1 or a tag).
UCLASS(meta = (DisplayName = "Sit on hero (pin him down)"))
class MYCHARACTERS_API UPrologueMonsterRole_SitOnHero : public UPrologueMonsterRole
{
	GENERATED_BODY()

public:
	UPrologueMonsterRole_SitOnHero();

	// How far from his centre it stands (flat, cm), on the side it came from. 0 = right on top of him.
	UPROPERTY(EditAnywhere, Category = "Role", meta = (ClampMin = "0.0"))
	float SitDistance = 40.0f;

	// The monster and the hero ignore each other's collision, so it can stand on him. Off: it stops against him.
	UPROPERTY(EditAnywhere, Category = "Role")
	bool bPassThroughHero = true;

	virtual FString Describe() const override;

protected:
	virtual bool GetGoal(const FPrologueRoleContext& Context, const FPrologueRoleMemberState& State, FVector& OutGoal, float& OutAcceptance) const override;
	virtual void UpdateMember(const FPrologueRoleContext& Context, FPrologueRoleMemberState& State) const override;
};

// A named group of monsters and its current role, kept by APrologueDirector (step 2.8). Members and States go in
// pairs. A member falls out by itself when it dies or someone rides it.
USTRUCT()
struct MYCHARACTERS_API FPrologueMonsterGroup
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<TWeakObjectPtr<ABaseMonster>> Members;

	UPROPERTY()
	TArray<FPrologueRoleMemberState> States;

	// Null = the members follow their own AI (the group stays, a later step can give it a role again).
	UPROPERTY()
	TObjectPtr<UPrologueMonsterRole> Role;

	// World time the role ends at (0 = until a step changes it).
	double EndTime = 0.0;

	bool bReleaseOnControlSwitch = false;
};

// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "HeroScriptCommand.generated.h"

class UAnimMontage;
class UAnimSequence;
class UHeroScriptRunner;
class USoundBase;
struct FAIRequestID;
struct FPathFollowingResult;

// Where a hero script command points: a placed marker, another hero, the nearest monster or a fixed spot.
UENUM(BlueprintType)
enum class EHeroScriptTargetType : uint8
{
	ActorWithTag		UMETA(DisplayName = "Actor with tag (nearest)"),
	Hero				UMETA(DisplayName = "Hero"),
	NearestMonster		UMETA(DisplayName = "Nearest living monster"),
	Location			UMETA(DisplayName = "World location"),
};

USTRUCT(BlueprintType)
struct MYCHARACTERS_API FHeroScriptTarget
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Target")
	EHeroScriptTargetType Type = EHeroScriptTargetType::ActorWithTag;

	// Actors with this tag placed in the level (TargetPoint etc.); the one nearest to the hero is used.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Target", meta = (EditCondition = "Type == EHeroScriptTargetType::ActorWithTag", EditConditionHides))
	FName Tag;

	// Which hero. Empty = the hero the player controls.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Target", meta = (EditCondition = "Type == EHeroScriptTargetType::Hero", EditConditionHides))
	FName HeroId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Target", meta = (EditCondition = "Type == EHeroScriptTargetType::Location", EditConditionHides))
	FVector Location = FVector::ZeroVector;

	// "tag 'Gate'", "hero Redneck", ...
	FString Describe() const;
};

UENUM()
enum class EHeroScriptCommandStatus : uint8
{
	Running,
	Done,
};

// One thing a hero does on his own in the prologue (task 04, step 2.5): walk somewhere, turn, wait, say a line...
// Picked from a drop-down in the scenario asset. UHeroScriptRunner (a component the director adds to the hero) runs
// the commands one after another while the hero is under his AI controller.
//
// The objects in the asset are templates: every time a command starts, the runner makes a fresh copy of it, so a
// command may keep its own runtime state in members (the asset is never written to). Commands use only public API
// of the hero (rule 1: heroes are not changed for the scenario).
UCLASS(Abstract, EditInlineNew, DefaultToInstanced, CollapseCategories)
class MYCHARACTERS_API UHeroScriptCommand : public UObject
{
	GENERATED_BODY()

public:
	// The command begins (on its fresh copy). The hero is valid and under his AI controller.
	virtual void Start(UHeroScriptRunner& Runner) {}

	// Every frame while the command runs. Done = the runner moves on to the next command.
	virtual EHeroScriptCommandStatus Tick(UHeroScriptRunner& Runner, float DeltaSeconds) { return EHeroScriptCommandStatus::Done; }

	// The command ends: finished (bInterrupted false), or cut short because another script started, the player took
	// the hero or he died. Undo what Start() changed on the hero (speed, a playing montage).
	virtual void End(UHeroScriptRunner& Runner, bool bInterrupted) {}

	// Short text for the log and the debug readout, e.g. "walk to tag 'Gate'".
	virtual FString Describe() const PURE_VIRTUAL(UHeroScriptCommand::Describe, return FString(););

	// How the running command is doing, for the debug readout ("12.3 m left, 0 cm/s"). Empty by default.
	virtual FString DescribeProgress(const UHeroScriptRunner& Runner) const { return FString(); }

	// Draws what the running command is doing for one frame (a move draws its path). Nothing by default.
	virtual void DrawDebug(const UHeroScriptRunner& Runner) const {}

	// False for commands that need the hero on his feet (walking, turning): while he lies after a scripted fall
	// (UScriptedKnockdownComponent, step 2.6) the runner skips them with a warning. True by default (lines, waits,
	// abilities, getting up).
	virtual bool CanRunWhileDown() const { return true; }
};

// A list of commands for one hero.
USTRUCT(BlueprintType)
struct MYCHARACTERS_API FHeroScript
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Instanced, BlueprintReadOnly, Category = "Script")
	TArray<TObjectPtr<UHeroScriptCommand>> Commands;

	// Start over after the last command: the "waiting" behaviour (the Priest keeps fighting monsters off, the Redneck
	// keeps struggling in the trap) that goes on until another script replaces it. A looping script never finishes.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Script")
	bool bLoop = false;
};

UENUM(BlueprintType)
enum class EHeroScriptMoveSpeed : uint8
{
	Walk			UMETA(DisplayName = "Walk"),
	Run				UMETA(DisplayName = "Run (sprint)"),
	Custom			UMETA(DisplayName = "Custom speed (crawl, limp)"),
};

// Walks, runs or crawls to the target along the navmesh. Moving targets (heroes, monsters) are followed.
UCLASS(meta = (DisplayName = "Move to"))
class MYCHARACTERS_API UHeroScriptCommand_MoveTo : public UHeroScriptCommand
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "Command")
	FHeroScriptTarget Target;

	// How close is "there", cm (from the capsule's edge, as the engine's path following counts it).
	UPROPERTY(EditAnywhere, Category = "Command", meta = (ClampMin = "0.0"))
	float AcceptanceRadius = 100.0f;

	UPROPERTY(EditAnywhere, Category = "Command")
	EHeroScriptMoveSpeed Speed = EHeroScriptMoveSpeed::Walk;

	// Speed for "Custom", cm/s (crawling with a trap on the leg ~60, limping ~120). Restored after the move.
	UPROPERTY(EditAnywhere, Category = "Command", meta = (ClampMin = "10.0", EditCondition = "Speed == EHeroScriptMoveSpeed::Custom", EditConditionHides))
	float CustomSpeed = 80.0f;

	// Optional: plays (and replays) while moving — a crawl or limp animation. Stopped when the move ends.
	UPROPERTY(EditAnywhere, Category = "Command")
	TObjectPtr<UAnimMontage> LoopMontage;

	// If the hero gets no closer for a few seconds three times in a row (a navmesh link he cannot really walk, a
	// blocked passage), he is put at the target with a warning on screen, so the scene still has him where it
	// needs him. Off: he stays where he got stuck and the script goes on. Either way the level should be fixed.
	UPROPERTY(EditAnywhere, Category = "Command")
	bool bTeleportIfStuck = true;

	virtual void Start(UHeroScriptRunner& Runner) override;
	virtual EHeroScriptCommandStatus Tick(UHeroScriptRunner& Runner, float DeltaSeconds) override;
	virtual void End(UHeroScriptRunner& Runner, bool bInterrupted) override;
	virtual FString Describe() const override;
	virtual FString DescribeProgress(const UHeroScriptRunner& Runner) const override;
	virtual void DrawDebug(const UHeroScriptRunner& Runner) const override;
	virtual bool CanRunWhileDown() const override { return false; }

private:
	void RequestMove(UHeroScriptRunner& Runner);
	void GiveUp(UHeroScriptRunner& Runner);
	void HandleMoveFinished(FAIRequestID RequestID, const FPathFollowingResult& Result);

	FDelegateHandle MoveFinishedHandle;
	TWeakObjectPtr<class UPathFollowingComponent> BoundPathFollowing;
	uint32 RequestId = 0;
	bool bMoveInFlight = false;
	bool bArrived = false;
	bool bWithoutPathfinding = false;
	int32 Attempts = 0;
	// Stuck detection: the closest the hero has got to the target during the current try, and when.
	float BestDistance = MAX_FLT;
	double ProgressTime = 0.0;
	double RetryTime = 0.0;
	bool bStartedSprint = false;
	bool bSpeedOverridden = false;
	float SavedMaxWalkSpeed = 0.0f;
};

// Turns the hero on the spot to face the target.
UCLASS(meta = (DisplayName = "Turn to"))
class MYCHARACTERS_API UHeroScriptCommand_TurnTo : public UHeroScriptCommand
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "Command")
	FHeroScriptTarget Target;

	// Degrees per second.
	UPROPERTY(EditAnywhere, Category = "Command", meta = (ClampMin = "10.0"))
	float TurnSpeed = 360.0f;

	virtual void Start(UHeroScriptRunner& Runner) override;
	virtual EHeroScriptCommandStatus Tick(UHeroScriptRunner& Runner, float DeltaSeconds) override;
	virtual FString Describe() const override;
	virtual bool CanRunWhileDown() const override { return false; }

private:
	double StartTime = 0.0;
};

// Does nothing for a while.
UCLASS(meta = (DisplayName = "Wait"))
class MYCHARACTERS_API UHeroScriptCommand_Wait : public UHeroScriptCommand
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "Command", meta = (ClampMin = "0.0"))
	float Seconds = 1.0f;

	virtual void Start(UHeroScriptRunner& Runner) override;
	virtual EHeroScriptCommandStatus Tick(UHeroScriptRunner& Runner, float DeltaSeconds) override;
	virtual FString Describe() const override;

private:
	double StartTime = 0.0;
};

// Plays an animation montage on the hero.
UCLASS(meta = (DisplayName = "Play montage"))
class MYCHARACTERS_API UHeroScriptCommand_PlayMontage : public UHeroScriptCommand
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "Command")
	TObjectPtr<UAnimMontage> Montage;

	UPROPERTY(EditAnywhere, Category = "Command", meta = (ClampMin = "0.1"))
	float PlayRate = 1.0f;

	// Off: the next command starts right away and the montage plays on top of it.
	UPROPERTY(EditAnywhere, Category = "Command")
	bool bWaitForEnd = true;

	virtual void Start(UHeroScriptRunner& Runner) override;
	virtual EHeroScriptCommandStatus Tick(UHeroScriptRunner& Runner, float DeltaSeconds) override;
	virtual void End(UHeroScriptRunner& Runner, bool bInterrupted) override;
	virtual FString Describe() const override;

private:
	bool bPlaying = false;
};

// A line the hero says: a subtitle on screen for now ("Redneck: Right!"), with an optional sound at the hero.
UCLASS(meta = (DisplayName = "Say a line"))
class MYCHARACTERS_API UHeroScriptCommand_Say : public UHeroScriptCommand
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "Command", meta = (MultiLine = "true"))
	FText Text;

	// Seconds on screen.
	UPROPERTY(EditAnywhere, Category = "Command", meta = (ClampMin = "0.5"))
	float Duration = 3.0f;

	// Optional voice line, played at the hero.
	UPROPERTY(EditAnywhere, Category = "Command")
	TObjectPtr<USoundBase> Sound;

	// On: the next command waits until the line is over. Off: the hero goes on while the subtitle stays up.
	UPROPERTY(EditAnywhere, Category = "Command")
	bool bWaitUntilDone = true;

	virtual void Start(UHeroScriptRunner& Runner) override;
	virtual EHeroScriptCommandStatus Tick(UHeroScriptRunner& Runner, float DeltaSeconds) override;
	virtual FString Describe() const override;

private:
	double StartTime = 0.0;
};

// Moves the hero to the target at once: a stand-in for climbing and jumping (onto the fence, off the monster)
// until those exist.
UCLASS(meta = (DisplayName = "Teleport (stand-in for climb/jump)"))
class MYCHARACTERS_API UHeroScriptCommand_Teleport : public UHeroScriptCommand
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "Command")
	FHeroScriptTarget Target;

	// The target marks the ground (a TargetPoint on the floor, a location): the hero's feet go there, not his
	// middle. Ignored for heroes and monsters (their centre is used).
	UPROPERTY(EditAnywhere, Category = "Command")
	bool bFeetOnTarget = true;

	// Take the target actor's facing (the marker's arrow). Off: the hero keeps facing the way he did.
	UPROPERTY(EditAnywhere, Category = "Command")
	bool bTakeTargetRotation = false;

	virtual void Start(UHeroScriptRunner& Runner) override;
	virtual FString Describe() const override;
};

// Hides the hero from the world (the Redneck vanishes behind the fence until the tavern) or brings him back.
// Hidden = not drawn (held items too), no collision, no movement.
UCLASS(meta = (DisplayName = "Hide / show hero"))
class MYCHARACTERS_API UHeroScriptCommand_Hide : public UHeroScriptCommand
{
	GENERATED_BODY()

public:
	// On: hide. Off: show again.
	UPROPERTY(EditAnywhere, Category = "Command")
	bool bHide = true;

	virtual void Start(UHeroScriptRunner& Runner) override;
	virtual FString Describe() const override;
};

// Uses one of the hero's abilities: calls a function without parameters on the hero or on one of his components
// (CloseEyes, OpenEyes, ToggleCrouch, StartSprinting on the hero; TogglePeek on UFingerFlamePeekComponent...).
// Any BlueprintCallable function works, also one defined in the hero's Blueprint.
UCLASS(meta = (DisplayName = "Use ability (call function)"))
class MYCHARACTERS_API UHeroScriptCommand_CallFunction : public UHeroScriptCommand
{
	GENERATED_BODY()

public:
	// Component of the hero the function is on. Empty = the hero himself.
	UPROPERTY(EditAnywhere, Category = "Command")
	TSubclassOf<UActorComponent> Component;

	// Function name exactly as in C++/Blueprint, e.g. "CloseEyes". Must have no parameters.
	UPROPERTY(EditAnywhere, Category = "Command")
	FName FunctionName;

	virtual void Start(UHeroScriptRunner& Runner) override;
	virtual FString Describe() const override;
};

// The hero falls (no damage, no panic) or gets up again: the same scripted fall as the "Knock hero down / get up"
// action (UScriptedKnockdownComponent, step 2.6), as a part of his own script — "runs, trips and falls". While he
// lies, "Move to" and "Turn to" are skipped; lines, waits and abilities still work.
UCLASS(meta = (DisplayName = "Fall down / get up"))
class MYCHARACTERS_API UHeroScriptCommand_KnockDown : public UHeroScriptCommand
{
	GENERATED_BODY()

public:
	// Off: he falls. On: he gets up.
	UPROPERTY(EditAnywhere, Category = "Command")
	bool bGetUp = false;

	// Fall or get-up animation, only for this moment (the fall's last frame is the lying pose). Empty = the hero's
	// own KnockdownFallAnimation / GetUpAnimation.
	UPROPERTY(EditAnywhere, Category = "Command")
	TObjectPtr<UAnimSequence> Animation;

	// On: the next command waits until the fall animation is over / until he is on his feet.
	UPROPERTY(EditAnywhere, Category = "Command")
	bool bWaitForAnimation = true;

	virtual void Start(UHeroScriptRunner& Runner) override;
	virtual EHeroScriptCommandStatus Tick(UHeroScriptRunner& Runner, float DeltaSeconds) override;
	virtual FString Describe() const override;

private:
	bool bStarted = false;
	double FallEndTime = 0.0;
};

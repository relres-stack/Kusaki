// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DayNightManager.generated.h"

class ADirectionalLight;

// Which part of the day CurrentTimeOfDay currently falls into — see ADayNightManager's threshold
// properties (DawnStartTime/DayStartTime/NightStartTime) for exactly where each boundary sits.
UENUM(BlueprintType)
enum class ESunPhase : uint8
{
	Night,
	Dawn,
	Day
};

// No payload — subscribers (e.g. ABaseMonster) just need to know a transition happened, not any data
// about it; CurrentTimeOfDay/SunPhase are already readable directly off the manager if a handler needs them.
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnDawnStarted);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnDayStarted);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnNightStarted);

// Drives a simple 24-hour day/night cycle: advances CurrentTimeOfDay every Tick, rotates a single
// DirectionalLight to sweep the sun across the sky to match, and broadcasts one Blueprint-assignable
// event per phase transition (Night->Dawn, Dawn->Day, Day->Night) so other systems — e.g. a monster
// that should only come out at night — can react without polling CurrentTimeOfDay/SunPhase themselves.
UCLASS()
class MYCHARACTERS_API ADayNightManager : public AActor
{
	GENERATED_BODY()

public:
	ADayNightManager();

	// The sun. Its actor rotation is driven entirely by CurrentTimeOfDay every Tick (see
	// UpdateSunRotation()) — whatever rotation it's placed at in the level is overwritten the first time
	// this manager ticks, so there's no need to hand-place it. Left unassigned (nullptr) is a valid,
	// harmless configuration — UpdateSunRotation() just no-ops — for a level that doesn't want a moving
	// sun at all.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Day Night|References")
	TObjectPtr<ADirectionalLight> DirectionalLight;

	// Desired real-world duration, IN MINUTES, of one full 24-in-game-hour cycle — this is the actual
	// designer-facing "how long should a full day/night cycle take to play out" tunable: BeginPlay()
	// converts it into DaySpeed below once at startup, so specifying "I want a full cycle to take about
	// 25 minutes" doesn't require hand-deriving an in-game-hours-per-real-second multiplier. Default
	// widened to 25 real minutes specifically so Dawn/Day/Night (proportionally, per their existing
	// DawnStartTime/DayStartTime/NightStartTime thresholds below — unchanged, so their RATIO to each
	// other is preserved, only the overall real-time scale grows) each get several real minutes of
	// genuinely playtestable window, instead of the previous DaySpeed=1.0 default's full cycle finishing
	// in about 24 REAL SECONDS (24 in-game hours / 1.0 in-game-hour-per-real-second) — nowhere near
	// enough time to observe any one phase, despite an old, incorrect comment here claiming "24 real
	// minutes".
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Day Night|Time", meta = (ClampMin = "1.0"))
	float FullCycleDurationMinutes = 25.0f;

	// In-game hours CurrentTimeOfDay advances per REAL-WORLD second — DERIVED from FullCycleDurationMinutes
	// above once in BeginPlay() (24.0 / (FullCycleDurationMinutes * 60.0)), not hand-authored: edit
	// FullCycleDurationMinutes instead, which is what actually controls overall cycle speed now.
	// VisibleAnywhere/BlueprintReadOnly (not EditAnywhere) for the same reason SunPhase below is —
	// letting this be hand-edited directly would just get silently overwritten by BeginPlay()'s
	// conversion the next time this actor starts play, which reads as a broken property rather than a
	// deliberately derived one. 0 (or negative) would freeze time entirely if it were ever reached, which
	// remains intentionally valid (e.g. a cutscene or a level that wants a fixed time of day) — it just
	// isn't reachable from a positive, ClampMin-1.0 FullCycleDurationMinutes in practice.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Day Night|Time")
	float DaySpeed = 1.0f;

	// Current in-game time, 0.0-24.0 (wraps back to 0 at 24, never reaches exactly 24 itself — see
	// Tick()'s FMath::Fmod). Editable directly (e.g. to jump straight to a specific time for testing)
	// rather than only ever advancing from Tick().
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Day Night|Time", meta = (ClampMin = "0.0", ClampMax = "24.0"))
	float CurrentTimeOfDay = 6.0f;

	// Read-only: derived every Tick from CurrentTimeOfDay vs. the thresholds below (see
	// ComputePhaseForTime()) — not meant to be hand-authored, since UpdateSunPhase() would just overwrite
	// it (and skip firing the transition event for whatever it was manually set to) on the very next Tick.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Day Night|Time")
	ESunPhase SunPhase = ESunPhase::Night;

	// CurrentTimeOfDay at which Night ends and Dawn begins. Widened from the original 5.0 default to 4.0
	// (a full 4-in-game-hour Dawn window against DayStartTime=8.0, up from 1.5) specifically to give
	// ABaseMonster's flee-to-shadow grace period (see its ApplySunDamage()/HandleDawnStarted()) enough
	// real time to actually reach cover before Day's full sun damage starts.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Day Night|Thresholds", meta = (ClampMin = "0.0", ClampMax = "24.0"))
	float DawnStartTime = 4.0f;

	// CurrentTimeOfDay at which Dawn ends and full Day begins. See DawnStartTime's comment on the widened
	// Dawn window this and it now bracket.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Day Night|Thresholds", meta = (ClampMin = "0.0", ClampMax = "24.0"))
	float DayStartTime = 8.0f;

	// CurrentTimeOfDay at which Day ends and Night begins again. Expected (not enforced) to be numerically
	// LARGER than DawnStartTime/DayStartTime — Night is the WRAPAROUND phase (>= NightStartTime OR <
	// DawnStartTime, see ComputePhaseForTime()), covering the stretch that crosses midnight.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Day Night|Thresholds", meta = (ClampMin = "0.0", ClampMax = "24.0"))
	float NightStartTime = 20.0f;

	// Fired once, the Tick a transition INTO Dawn is detected (Night->Dawn only — see UpdateSunPhase();
	// never re-fires while already in Dawn). Bind e.g. from a monster's BeginPlay() to react to sunrise
	// starting.
	UPROPERTY(BlueprintAssignable, Category = "Day Night|Events")
	FOnDawnStarted OnDawnStarted;

	// Fired once, the Tick a transition INTO full Day is detected (Dawn->Day only).
	UPROPERTY(BlueprintAssignable, Category = "Day Night|Events")
	FOnDayStarted OnDayStarted;

	// Fired once, the Tick a transition INTO Night is detected (Day->Night only).
	UPROPERTY(BlueprintAssignable, Category = "Day Night|Events")
	FOnNightStarted OnNightStarted;

protected:
	virtual void BeginPlay() override;

public:
	virtual void Tick(float DeltaTime) override;

private:
	// Sets DirectionalLight's actor rotation from CurrentTimeOfDay — a single Pitch sweep (Yaw/Roll fixed
	// at 0) across a full 360-degree arc: Pitch = 90 - (CurrentTimeOfDay / 24) * 360, so 06:00 sits at
	// Pitch 0 (sun exactly at the horizon, rising), 12:00 at Pitch -90, and 00:00 at Pitch 90.
	// NOTE THE SIGN: a DirectionalLight shines ALONG its own forward vector (FRotator(Pitch,0,0).Vector()
	// has Z = sin(Pitch)), so a noon sun overhead — rays traveling straight DOWN onto the ground — needs
	// Pitch = -90 (Forward.Z = -1), not +90. The naive "Pitch = (Time/24)*360 - 90" mapping gets this
	// backwards (Forward.Z = +1 at noon, i.e. rays traveling straight UP, away from the ground), which is
	// what previously made every gameplay system reading "toward the sun" off this light's rotation
	// (ABaseMonster::CheckSunExposure()/FindShadowLocation(), both using
	// -DirectionalLight->GetActorForwardVector() as the correct "direction toward the sun" convention)
	// see exactly inverted day/night behavior — sun exposure traces pointed into the floor at real noon and
	// into open sky at real midnight. No-ops if DirectionalLight isn't assigned.
	void UpdateSunRotation();

	// Recomputes SunPhase from CurrentTimeOfDay via ComputePhaseForTime() and, if it actually changed,
	// broadcasts the matching OnDawnStarted/OnDayStarted/OnNightStarted event exactly once for that
	// transition.
	void UpdateSunPhase();

	// Pure threshold lookup, no side effects — used by both UpdateSunPhase() (every Tick) and BeginPlay()
	// (to seed SunPhase's initial value without firing a spurious transition event before anything's had a
	// chance to bind to it).
	ESunPhase ComputePhaseForTime(float TimeOfDay) const;
};

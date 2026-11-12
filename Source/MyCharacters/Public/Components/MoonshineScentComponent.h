// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MoonshineScentComponent.generated.h"

// ARedneckCharacter's post-Rage-Dash "scent trail": after a dash, the Redneck periodically gives away
// their position to nearby monsters for a short while, even standing still — the moonshine smell lingers.
// Entirely OUTSIDE UMoonshineComponent (a stable, do-not-touch mechanic — see CLAUDE.md's "Реестр
// механик") — this only calls that component's dash indirectly, via
// ARedneckCharacter::TryHijackAttackPress() already having triggered PerformRageDash().
//
// Deliberately does NOT use MakeNoise()/a perception component — ABaseMonster has no
// AIPerceptionComponent/PawnSensingComponent listening for that at all (dead path, confirmed by reading
// the code). Instead reuses the same already-proven mechanism ABaseCharacter::RegisterMouthGasp() already
// uses for "call for a monster's attention" — ABaseCharacter::FindNearbyMonster(Radius) to find any live
// monster within ScentSearchRadius, then that monster's OWN public ABaseMonster::BroadcastAlert() to have
// it pull in its own pack via its own AlertRadius, exactly like a mouth-gasp or Naskok alert would.
//
// Flow: StartScentTrail() arms a repeating ScentPulseInterval timer plus a one-shot ScentDuration stop
// timer. Calling it again before the previous window ends just re-arms both timers (same "re-arm restarts
// cleanly" pattern URageFallComponent::StartDashResolutionWindow() already uses) — no stacking, no
// separate cooldown. Each pulse: find any live monster within ScentSearchRadius; if one exists,
// BroadcastAlert() from the REDNECK'S location (not the monster's) — a miss (nobody nearby) is not an
// error, just a quiet pulse.
//
// Triggered automatically, NOT from the dash itself: TickComponent() watches
// ARedneckCharacter::MoonshineComponent->bIsMoonshineActive and calls StartScentTrail() itself on the
// true -> false transition (the buff wearing off), whether or not a Rage Dash ever happened during it.
// Deliberately NOT tied to PerformRageDash()/TryHijackAttackPress() — while the buff is still active,
// IsImmuneToMonsterCatch() already makes any resulting alert unfelt, and monsters are already reacting to
// the dash itself (noise/Naskok/chase) through their own normal means; the hangover scent is meant to be a
// SEPARATE, LATER threat once that attention has settled, not layered on top of it. See
// URageFallComponent's own header comment for why ITS stumble stays tied to the dash instead — the two
// debuffs are deliberately on different timings.
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class MYCHARACTERS_API UMoonshineScentComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMoonshineScentComponent();

	// Seconds after a Rage Dash the scent trail lingers — how long pulses keep firing.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Moonshine Scent")
	float ScentDuration = 6.0f;

	// Seconds between each "does anything nearby smell it" pulse within the ScentDuration window.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Moonshine Scent")
	float ScentPulseInterval = 1.5f;

	// Radius (world units) each pulse searches for ANY live monster to act as the alert's trigger — NOT
	// the alert's own spread radius, which is that found monster's own ABaseMonster::AlertRadius (3500 by
	// default) once BroadcastAlert() fires.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Moonshine Scent")
	float ScentSearchRadius = 1200.0f;

	// Arms the scent trail. Called automatically by TickComponent() on the moonshine buff's true -> false
	// transition — see this class's own header comment on why. Left public/BlueprintCallable too in case a
	// manual trigger is ever useful. Re-armable: calling again before ScentDuration elapses just restarts
	// both timers from zero.
	UFUNCTION(BlueprintCallable, Category = "Moonshine Scent")
	void StartScentTrail();

protected:
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	// Timer callback for ScentPulseTimerHandle — fires every ScentPulseInterval seconds while the trail is
	// active. Finds any live monster within ScentSearchRadius and, if one exists, has IT broadcast an
	// alert centered on the Redneck's own location; logs either outcome.
	void OnScentPulse();

	// Timer callback for ScentDurationTimerHandle — fires ScentDuration seconds after StartScentTrail(),
	// stops the repeating pulse timer. The trail is over until the buff's next end re-arms it.
	void StopScentTrail();

	// MoonshineComponent->bIsMoonshineActive as of the previous tick — what TickComponent() compares
	// against to detect the true -> false transition (the buff just wearing off).
	bool bWasMoonshineActiveLastTick = false;

	FTimerHandle ScentPulseTimerHandle;
	FTimerHandle ScentDurationTimerHandle;
};

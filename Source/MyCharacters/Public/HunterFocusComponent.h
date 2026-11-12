// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "HunterFocusComponent.generated.h"

class UMaterialInterface;

// ARedneckCharacter's "Hunter's Focus" (Охотничий глазомер) ability: pressing F applies a brief global
// slow-motion window and, at the moment of activation, traces forward from the camera to whatever's
// ahead, converts that distance into a whole number of "steps," and spawns real (not DrawDebug — those
// are compiled out of Shipping builds entirely, unacceptable for a real gameplay ability) marker actors
// on the ground at each step interval, plus a HUD readout (see ABaseCharacter::GetHunterFocusPromptText()/
// BuildInteractionPromptText()) showing the count. A snapshot taken once at activation, not a continuously
// re-traced radar — matches the "a hunter takes a moment to size up the shot" flavor and avoids
// respawning markers every frame. Self-contained UActorComponent (Moonshine's own precedent) so no other
// hero carries any Focus-specific state.
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class MYCHARACTERS_API UHunterFocusComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UHunterFocusComponent();

	// Seconds Focus stays active before automatically deactivating — measured in DILATED (slowed) game
	// time, same as any other GetWorldTimerManager() timer once TimeDilationFactor is applied: at the
	// default 0.5, a 3-second FocusDuration plays out over ~6 REAL-world seconds, which is the actual
	// point of a bullet-time-style ability (more real time to think, not less).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hunter's Focus")
	float FocusDuration = 3.0f;

	// Real-time seconds (Focus has already reverted GlobalTimeDilation to 1.0 by the time this starts —
	// see DeactivateFocus()) before bCanUseFocus goes back to true.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hunter's Focus")
	float FocusCooldown = 5.0f;

	// UGameplayStatics::SetGlobalTimeDilation() value applied for the duration of an active Focus — 0.5
	// means half speed. Global (not CustomTimeDilation on just this character) so monsters/physics/
	// everything else visibly slows too, matching the "focused, slowed-down atmosphere" the ability
	// is meant to create.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hunter's Focus")
	float TimeDilationFactor = 0.5f;

	// Max range (world units) the activation trace (see ActivateFocus()) reaches — independent of
	// ABaseCharacter::AimTraceDistance (the aim/throw reticle's own range); see
	// ABaseCharacter::TraceHunterFocusTarget()'s own comment.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hunter's Focus")
	float FocusTraceDistance = 1500.0f;

	// World units per "step" — StepsToTarget = round(HitDistance / StepDistance), and step markers are
	// spawned at every multiple of this distance out to StepsToTarget.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hunter's Focus")
	float StepDistance = 100.0f;

	// True for FocusDuration seconds after a successful ToggleFocus() activation.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hunter's Focus")
	bool bIsFocusActive = false;

	// True once FocusCooldown has elapsed since the last Focus ended — ToggleFocus() refuses to activate
	// while this is false.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hunter's Focus")
	bool bCanUseFocus = true;

	// Step count computed by the most recent ActivateFocus() — what the HUD prompt (see
	// ABaseCharacter::GetHunterFocusPromptText()) reads. Stays at its last value after Focus ends (the
	// prompt itself stops showing once !bIsFocusActive, so a stale number here is never actually
	// displayed) rather than resetting to 0, purely so nothing needs to special-case "just ended" vs
	// "never activated."
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hunter's Focus")
	int32 StepsToTarget = 0;

	// True if the most recent activation's trace actually hit an ABaseMonster (vs a wall/floor/nothing) —
	// GetHunterFocusPromptText() reads this to choose "STEPS TO TARGET" vs "SAFE STEPS" wording.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hunter's Focus")
	bool bTargetIsMonster = false;

	// Toggles Focus: activates it if currently inactive, bCanUseFocus, AND the owner is currently aiming
	// (ABaseCharacter::GetIsAiming(), RMB held) — deactivates it immediately (same cleanup +
	// cooldown-start as letting it expire naturally — see DeactivateFocus()) if already active. No-ops
	// (with an on-screen reason) while on cooldown or not aiming. Bound to F by
	// ARedneckCharacter::SetupPlayerInputComponent(); while active, releasing Aim also interrupts it — see
	// TickComponent().
	UFUNCTION(BlueprintCallable, Category = "Hunter's Focus")
	void ToggleFocus();

	// Color (and opacity, via its alpha) MentalTrailMaterial's exposed "Color" parameter is set to for the
	// "Mental Trail" state — a faint, glowing blue by default. Low alpha (0.35) is what makes the markers
	// read as semi-transparent.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hunter's Focus|Mental Trail")
	FLinearColor MentalTrailColor = FLinearColor(0.25f, 0.6f, 1.0f, 0.35f);

	// Switches every currently-spawned step marker between its normal appearance and the translucent,
	// glowing "Mental Trail" material (see MentalTrailMaterial/MentalTrailColor) — called by
	// ARedneckCharacter::OnEyesClosedChanged() so closing eyes transitions the markers instead of hiding
	// or destroying them. Also applied automatically to freshly-spawned markers in ActivateFocus() if eyes
	// are already closed at that moment, and safe to call with no markers present (a no-op loop). If eyes
	// are reopening (bActive false) AND Focus has already ended (!bIsFocusActive) — i.e. the player just
	// finished looking at a leftover trail from a Focus that's long over — this also clears the markers,
	// since nothing else would otherwise ever do so in that case (see DeactivateFocus()'s own comment).
	UFUNCTION(BlueprintCallable, Category = "Hunter's Focus|Mental Trail")
	void SetMentalTrailActive(bool bActive);

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// Only enabled (see SetComponentTickEnabled() calls in ActivateFocus()/DeactivateFocus()) while
	// bIsFocusActive — polls ABaseCharacter::GetIsAiming() every frame so releasing Aim (RMB) mid-Focus
	// interrupts it immediately, same "authoritative every-tick enforcement" reasoning ABaseCharacter's
	// own Tick() already uses for bIsAiming itself (see its comment on IsInputKeyDown()).
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	// No-ops (with an on-screen reason) unless ABaseCharacter::GetIsAiming() is true — Hunter's Focus is
	// an aimed ability, not a free F-press; see ToggleFocus()'s own header comment.
	void ActivateFocus();

	// Shared cleanup for the natural FocusDuration timeout, a manual mid-Focus ToggleFocus(), AND
	// TickComponent() detecting Aim released — reverts GlobalTimeDilation to 1.0 and starts
	// FocusCooldown. Idempotent (no-ops if !bIsFocusActive) so it's safe as both a direct call and a
	// timer callback that might otherwise fire again after an early manual toggle-off already ran it.
	// Deliberately does NOT clear the spawned step markers — see this function's own .cpp comment on why
	// (that used to happen here and broke Mental Trail); ActivateFocus()'s next snapshot,
	// SetMentalTrailActive(), and EndPlay() are what actually clear them now.
	void DeactivateFocus();

	void EndCooldown();

	// Spawns one small marker actor on the ground at each step interval along Direction from Origin, out
	// to StepsToTarget — a short downward trace per step places it on the actual ground rather than
	// assuming flat terrain.
	void SpawnStepMarkers(const FVector& Origin, const FVector& Direction);

	void ClearStepMarkers();

	UPROPERTY()
	TArray<TObjectPtr<AActor>> StepMarkers;

	// The marker mesh's own default material, captured once in the constructor — what SetMentalTrailActive
	// (false) restores a marker's material to. Same asset every marker starts with (they all share
	// MarkerMesh), so one cached reference covers all of them rather than tracking it per-marker.
	UPROPERTY()
	TObjectPtr<UMaterialInterface> DefaultMarkerMaterial;

	// Translucent, unlit engine debug material (/Engine/EngineDebugMaterials/M_SimpleUnlitTranslucent) —
	// exposes a "Color" vector parameter that drives both base and emissive color, which is what makes a
	// UMaterialInstanceDynamic of this read as "faintly glowing" at low alpha rather than a flat tint.
	// Loaded via ConstructorHelpers rather than authoring a new Material asset for this project — same
	// "reuse a guaranteed-present engine asset" precedent as MarkerMesh's own Cylinder.
	UPROPERTY()
	TObjectPtr<UMaterialInterface> MentalTrailMaterial;

	// Mirrors ABaseCharacter::IsEyesClosed() at the time of the most recent SetMentalTrailActive() call —
	// read by ActivateFocus() so freshly-spawned markers immediately get the right material instead of
	// defaulting to "normal" until the next eyes-open/closed toggle.
	bool bMentalTrailActive = false;

	FTimerHandle FocusDurationTimerHandle;
	FTimerHandle CooldownTimerHandle;
};

// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "FingerFlamePeekComponent.generated.h"

class ABaseCharacter;
class UPointLightComponent;
class USilhouetteVisionComponent;

// APriestCharacter's "Огонёк на пальцах" (Finger Flame) ability: pressing F, while the eyes are ALREADY
// closed (see TogglePeek()'s own guard), lets the Priest "peek" at the surrounding space in silhouette
// for PeekDuration seconds WITHOUT opening the eyes — bIsEyesClosed stays true for the whole thing, so
// Rule 1.1's background threat (ABaseMonster::TryStartChaseAttack() polling IsEyesClosed()) never sees
// this as exposure. Thematically parallel to the future "Чернильные капли" (ink drops) mechanic — both
// are "see in B/W silhouette without opening the eyes" — see implementation-roadmap.md's 1.1b notes.
// Gameplay-logic/state ONLY for now: bIsPeekActive is the hook a future visual/post-process silhouette
// effect will read, but no rendering happens yet (same deferral already agreed for ink drops). Self-
// contained UActorComponent (UHunterFocusComponent's own precedent) so no other hero carries any
// Peek-specific state.
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class MYCHARACTERS_API UFingerFlamePeekComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UFingerFlamePeekComponent();

	// Seconds the peek stays active before automatically ending.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Finger Flame")
	float PeekDuration = 5.0f;

	// Seconds after a peek ends before bCanUsePeek goes back to true — same "can't spam it back-to-back"
	// pattern as UHunterFocusComponent::FocusCooldown.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Finger Flame")
	float PeekCooldown = 5.0f;

	// True for PeekDuration seconds after a successful TogglePeek() activation.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Finger Flame")
	bool bIsPeekActive = false;

	// True once PeekCooldown has elapsed since the last peek ended — TogglePeek() refuses to activate
	// while this is false.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Finger Flame")
	bool bCanUsePeek = true;

	// The actual light — assigned by APriestCharacter's constructor right after creating it, same
	// "component can't CreateDefaultSubobject a sibling" reasoning as UFlashlightComponent::LightComponent.
	// ActivatePeek()/DeactivatePeek() toggle its visibility; harmlessly no-op if never assigned.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Finger Flame")
	TObjectPtr<UPointLightComponent> LightComponent;

	// The shared silhouette-vision screen effect — assigned by APriestCharacter's constructor right after
	// creating it, same "component can't CreateDefaultSubobject a sibling" reasoning as LightComponent
	// above. ActivatePeek()/DeactivatePeek() toggle it (no Duration passed — this component's own
	// PeekDuration timer is what decides when DeactivateVision() gets called); harmlessly no-op if never
	// assigned.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Finger Flame")
	TObjectPtr<USilhouetteVisionComponent> VisionComponent;

	// Toggles the peek: activates it if currently inactive, bCanUsePeek is true, AND the owning
	// ABaseCharacter's eyes are already closed (IsEyesClosed() — the entire point of this ability is
	// "see without opening your eyes," so it's a silent no-op with eyes open rather than doing something
	// meaningless). Deactivates it immediately (same cleanup + cooldown-start as letting it expire
	// naturally — see DeactivatePeek()) if already active. No-ops while on cooldown. Bound to F by
	// APriestCharacter::SetupPlayerInputComponent(). Deliberately never touches bIsEyesClosed in either
	// direction — that's the whole safety guarantee of this ability.
	UFUNCTION(BlueprintCallable, Category = "Finger Flame")
	void TogglePeek();

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void ActivatePeek();

	// Shared cleanup for both the natural PeekDuration timeout AND a manual mid-peek TogglePeek() —
	// clears bIsPeekActive and starts PeekCooldown. Idempotent (no-ops if !bIsPeekActive).
	void DeactivatePeek();

	void EndCooldown();

	FTimerHandle PeekDurationTimerHandle;
	FTimerHandle CooldownTimerHandle;
};

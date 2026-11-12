// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MonsterAudioComponent.generated.h"

class ABaseMonster;
class UAudioComponent;
class USoundBase;

// Monster sound design — a completely standalone mechanic per CLAUDE.md rule 1 ("одна механика =
// один компонент"): four independent sound triggers (groan/breathing/growl/attack-hit), all driven
// purely by reading ABaseMonster's already-public state (GetMesh(), IsScanningForPlayer()) — nothing
// in ABaseMonster.cpp/.h is touched beyond the one-line component creation in its constructor (rule 3:
// mechanics talk to each other through public reads, not by rewriting each other's bodies). Every
// sound field defaults to nullptr and every trigger no-ops silently without one assigned, so this
// compiles and runs identically to before on any monster that hasn't had sounds set up yet — a
// designer drops sound assets into a monster Blueprint's Class Defaults with no further code changes.
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class MYCHARACTERS_API UMonsterAudioComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMonsterAudioComponent();

	// Background groan — expected to be an author-side LOOPING sound (a looping Sound Cue or a
	// pre-looped wave); this component only calls Play() once in BeginPlay() and never restarts it.
	// Ordinary 3D spatialized audio on the monster's own mesh already gives distance falloff — no
	// dedicated USoundAttenuation asset is required for this task, a designer can add one on the
	// sound asset/component later without further code changes.
	UPROPERTY(EditAnywhere, Category = "Audio")
	TObjectPtr<USoundBase> GroanSound;

	// Breathing — also expected to be looping. Plays while the player is within
	// BreathingProximityRadius of this monster (see TickComponent()).
	UPROPERTY(EditAnywhere, Category = "Audio")
	TObjectPtr<USoundBase> BreathingSound;

	// Growl — also expected to be looping. Plays while this monster is IsScanningForPlayer() (irritated
	// after a missed swing).
	UPROPERTY(EditAnywhere, Category = "Audio")
	TObjectPtr<USoundBase> GrowlSound;

	// One-shot attack-hit sound. Not called from anywhere in C++ — see PlayAttackHitSound()'s own
	// comment on why this is meant to be triggered by an Anim Notify placed on the existing attack
	// montages in the editor, not a code call site.
	UPROPERTY(EditAnywhere, Category = "Audio")
	TObjectPtr<USoundBase> AttackHitSound;

	// Distance (world units) within which BreathingSound plays — deliberately INDEPENDENT of
	// ABaseMonster::CatchDistance (150.0f), not read from it: a bit larger than CatchDistance so
	// breathing acts as an early warning cue BEFORE a real hit lands, rather than exactly
	// synchronized with it. Tune in PIE.
	UPROPERTY(EditAnywhere, Category = "Audio")
	float BreathingProximityRadius = 250.0f;

	// One-shot attack-hit sound, played at the owning monster's current location. Intended to be wired
	// up as a BlueprintCallable Anim Notify target on this monster's existing attack montages
	// (Naskok/Chase-attack) in the editor — deliberately not called from any ABaseMonster C++ function,
	// so the attack-hit logic in ABaseMonster.cpp stays completely untouched. No-op if AttackHitSound
	// is unset.
	UFUNCTION(BlueprintCallable, Category = "Audio")
	void PlayAttackHitSound();

protected:
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	// The owning monster, cached in BeginPlay() — every read this component does (GetMesh(),
	// IsScanningForPlayer(), GetActorLocation()) goes through this.
	TWeakObjectPtr<ABaseMonster> OwnerMonster;

	// Dynamically created sub-components, same "NewObject<>/RegisterComponent(), plain private field,
	// not UPROPERTY" pattern already used elsewhere in this project for similar runtime-created
	// components (see ABaseCharacter::EyesClosedPostProcessComp/APriestCharacter::FingerFlameLight).
	// GroanAudioComp starts playing immediately in BeginPlay() (if GroanSound is set) and is never
	// stopped; BreathingAudioComp/GrowlAudioComp start silent and are toggled by TickComponent().
	TObjectPtr<UAudioComponent> GroanAudioComp;
	TObjectPtr<UAudioComponent> BreathingAudioComp;
	TObjectPtr<UAudioComponent> GrowlAudioComp;

	// Edge-detection so TickComponent() only calls Play()/Stop() on an actual state transition, not
	// every frame the condition happens to still be true (which would otherwise keep restarting an
	// already-playing looping sound).
	bool bWasBreathingLastTick = false;
	bool bWasGrowlingLastTick = false;
};

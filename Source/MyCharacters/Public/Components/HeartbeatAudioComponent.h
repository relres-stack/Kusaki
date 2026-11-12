// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "HeartbeatAudioComponent.generated.h"

class ABaseCharacter;
class UAudioComponent;
class USoundBase;

// Universal hero heartbeat/breathing — a fully standalone mechanic per CLAUDE.md rule 1 ("одна
// механика = один компонент"), created in ABaseCharacter's constructor so every hero gets it
// regardless of subclass (same "every hero gets it" precedent as UInventoryComponent/UProbeComponent/
// UHidingComponent). Reads only the already-public ABaseCharacter::bIsPanicking — nothing in
// ABaseCharacter.cpp/.h is rewritten beyond the one-line component creation in its constructor (rule
// 3: mechanics talk to each other through public reads, not by editing each other's bodies). All three
// sound fields default to nullptr and every trigger no-ops silently without them assigned, so this
// compiles and runs identically to before on any hero that hasn't had sounds set up yet.
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class MYCHARACTERS_API UHeartbeatAudioComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UHeartbeatAudioComponent();

	// Calm, quiet heartbeat — expected to be an author-side LOOPING sound (a looping Sound Cue or a
	// pre-looped wave); this component only calls Play()/Stop() once per transition, never restarts it
	// mid-loop. Plays while NOT panicking.
	UPROPERTY(EditAnywhere, Category = "Audio")
	TObjectPtr<USoundBase> CalmHeartbeatSound;

	// Calm breathing — also expected to be looping. Plays alongside CalmHeartbeatSound.
	UPROPERTY(EditAnywhere, Category = "Audio")
	TObjectPtr<USoundBase> CalmBreathingSound;

	// Panic heartbeat — also expected to be looping. Replaces BOTH calm sounds for the duration of
	// ABaseCharacter::bIsPanicking.
	UPROPERTY(EditAnywhere, Category = "Audio")
	TObjectPtr<USoundBase> PanicHeartbeatSound;

protected:
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	// The owning character, cached in BeginPlay() — every read this component does (bIsPanicking,
	// GetRootComponent()) goes through this.
	TWeakObjectPtr<ABaseCharacter> OwnerCharacter;

	// Dynamically created sub-components, same "NewObject<>/RegisterComponent(), plain private field,
	// not UPROPERTY" pattern already used elsewhere in this project for similar runtime-created
	// components (see ABaseCharacter::EyesClosedPostProcessComp/UMonsterAudioComponent's own audio
	// comps). Non-spatialized (bAllowSpatialization = false) — this is the player's own body sound, not
	// something coming from a position in the world, so it shouldn't attenuate with camera distance.
	TObjectPtr<UAudioComponent> CalmHeartbeatComp;
	TObjectPtr<UAudioComponent> CalmBreathingComp;
	TObjectPtr<UAudioComponent> PanicHeartbeatComp;

	// Edge-detection so TickComponent() only calls Play()/Stop() on an actual bIsPanicking transition,
	// not every frame it happens to still be true/false (which would otherwise keep restarting an
	// already-playing looping sound) — same edge-triggered reasoning as ABaseCharacter::Move()'s own
	// PanicMovementPenalty per-direction flags.
	bool bWasPanickingLastTick = false;
};

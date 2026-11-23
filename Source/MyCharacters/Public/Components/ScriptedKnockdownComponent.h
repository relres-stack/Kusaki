// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ScriptedKnockdownComponent.generated.h"

class ABaseCharacter;
class UAnimSequence;
class USkeletalMeshComponent;

UENUM(BlueprintType)
enum class EScriptedKnockdownState : uint8
{
	Standing,
	Lying,
	GettingUp,
};

// A scripted fall in the prologue (task 04, step 2.6): the hero falls, lies until the scenario tells him to get up,
// then stands up. Unlike a monster's knockdown it does no damage and starts no panic: it is part of the scene (the
// Redneck knocks the Priest down, a hero trips and falls), not a hit.
//
// While he lies the hero cannot walk, jump, crouch or aim, but the player can still look around and use the abilities
// that do not move him (the Priest's inflamed vision, eyes, finger flame) — step 11 of the scenario needs exactly that.
//
// APrologueDirector adds this component to a hero at runtime the first time the scenario knocks him down; it is not
// created in ABaseCharacter's constructor (rule 1: heroes are not changed for the scenario). It uses only the hero's
// public API and his mesh.
//
// How the lying state is kept: the fall animation plays on the mesh in single-node mode (the same way the panic
// knockdown does it, see ABaseCharacter::PlayKnockdownAnimation(), so no slot is needed in the AnimBP) and its last
// frame holds. Movement is switched off (MOVE_None). Both are checked again every frame, because other code puts
// them back: every possession (a control switch) calls ACharacter::Restart(), which sets walking again, and a panic
// episode ends with the AnimBP restored. While the hero panics, the panic owns his pose; the fall pose comes back
// once the panic is over.
UCLASS(ClassGroup = (Prologue), meta = (BlueprintSpawnableComponent))
class MYCHARACTERS_API UScriptedKnockdownComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UScriptedKnockdownComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// The hero falls: FallAnimation plays once and its last frame holds while he lies. Null = the hero's own
	// KnockdownFallAnimation (the panic fall). If he is in the air he lands first. Already lying = nothing happens.
	// Returns false (and logs why) when he cannot fall: dead, or riding a monster (dismount him first).
	// bUntouchableWhileDown: monsters cannot harm him until he is fully up again (step 2.7) — APrologueDirector reads
	// IsUntouchableWhileDown() and sets the hero's flag; this component does not touch it itself.
	UFUNCTION(BlueprintCallable, Category = "Scripted Knockdown")
	bool KnockDown(UAnimSequence* FallAnimation = nullptr, bool bUntouchableWhileDown = true);

	// The hero stands up: GetUpAnimation plays once (null = the hero's own GetUpAnimation; none at all = he is up at
	// once), then walking and the AnimBP come back. Returns false if he is not lying.
	UFUNCTION(BlueprintCallable, Category = "Scripted Knockdown")
	bool GetUp(UAnimSequence* GetUpAnimation = nullptr);

	// Lying or still getting up.
	UFUNCTION(BlueprintPure, Category = "Scripted Knockdown")
	bool IsDown() const { return State != EScriptedKnockdownState::Standing; }

	UFUNCTION(BlueprintPure, Category = "Scripted Knockdown")
	EScriptedKnockdownState GetState() const { return State; }

	// Down (lying or getting up) after a fall that asked for protection (KnockDown()'s bUntouchableWhileDown).
	UFUNCTION(BlueprintPure, Category = "Scripted Knockdown")
	bool IsUntouchableWhileDown() const { return IsDown() && bDownIsUntouchable; }

	// Seconds the fall animation takes (0 if there is none): a script command waits this long for the fall to end.
	float GetFallDuration() const;

	// "lying", "getting up (0.8 s)", "standing" — for the debug readout.
	FString DescribeState() const;

private:
	ABaseCharacter* GetHero() const;

	// Every frame while down: no walking (once he is on the ground), no aiming, and (while lying) the fall pose.
	void HoldDown(ABaseCharacter& Hero);
	void FinishGettingUp(ABaseCharacter& Hero);

	// Plays Anim once on the mesh in single-node mode; bAtEnd jumps straight to its last frame (the pose is put back
	// after something else took the mesh over, without replaying the fall).
	static void PlayPose(USkeletalMeshComponent& Mesh, UAnimSequence* Anim, bool bAtEnd);

	void Notify(const FString& Message, const FColor& Color) const;

	EScriptedKnockdownState State = EScriptedKnockdownState::Standing;

	bool bDownIsUntouchable = true;

	// The animation whose last frame is the lying pose (null = none was found: he only stops, still standing).
	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> LyingAnimation;

	double GetUpEndTime = 0.0;
};

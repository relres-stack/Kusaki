// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "MonsterAnimInstance.generated.h"

class ABaseMonster;

// Set this as an Animation Blueprint's Parent Class (Class Settings) to get Speed/IsChasing/etc. below
// as ready-made variables in its AnimGraph — e.g. ABP_Whisper, targeting SKEL_Whisper_Skeleton, with its
// locomotion state machine's transition rules reading Speed directly, no Blueprint-side event graph
// logic needed to compute any of them. Every value here is pulled from the owning ABaseMonster's own
// already-existing BlueprintCallable animation getters (GetMovementSpeed()/GetIsChasing()/etc. — see
// their own header comments on BaseMonster.h) each frame in NativeUpdateAnimation(), so this class is
// intentionally thin: it doesn't duplicate any AI/state logic, only exposes what's already there.
UCLASS()
class MYCHARACTERS_API UMonsterAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	// Current horizontal movement speed (units/sec) — drive a locomotion state machine's Idle<->Walk/Run
	// transition rules and/or a speed-driven Blend Space directly from this.
	UPROPERTY(BlueprintReadOnly, Category = "Monster Animation")
	float Speed = 0.0f;

	// True while chasing the player.
	UPROPERTY(BlueprintReadOnly, Category = "Monster Animation")
	bool bIsChasing = false;

	// True while Stalking (holding position at a detected player's light boundary).
	UPROPERTY(BlueprintReadOnly, Category = "Monster Animation")
	bool bIsStalking = false;

	// True while fleeing to (or already standing safely in) shadow — see bIsIdleInShadow below to tell
	// the two apart.
	UPROPERTY(BlueprintReadOnly, Category = "Monster Animation")
	bool bIsFleeingToShadow = false;

	// True specifically once a shadow flee has ARRIVED and stopped moving (bIsFleeingToShadow stays true
	// for the whole episode, covering both "still running" and "arrived" — this flag distinguishes them).
	UPROPERTY(BlueprintReadOnly, Category = "Monster Animation")
	bool bIsIdleInShadow = false;

protected:
	virtual void NativeInitializeAnimation() override;
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;

private:
	// Cached once in NativeInitializeAnimation() via TryGetPawnOwner() — the ABaseMonster this AnimGraph
	// is actually animating. Null (and NativeUpdateAnimation() a no-op, all variables stay at their last
	// value) for a preview/editor context with no real pawn possessing this mesh.
	UPROPERTY()
	TObjectPtr<ABaseMonster> OwningMonster;
};

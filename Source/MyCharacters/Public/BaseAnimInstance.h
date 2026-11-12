// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "BaseAnimInstance.generated.h"

class ABaseCharacter;

// Set this as an Animation Blueprint's Parent Class (Class Settings) to get bIsCrouched below as a
// ready-made variable in its AnimGraph — every BP_* hero (BP_Redneck/BP_Priest/BP_Rider/BP_Sibling, all
// ARedneckCharacter/APriestCharacter/ARiderCharacter/ASiblingCharacter, all ABaseCharacter) can drive its
// own crouched Idle/Walk state directly from this, no Blueprint-side Cast-to-ABaseCharacter+IsCrouched()
// event-graph node needed. Same "thin, doesn't duplicate any state logic, only exposes what the owning
// actor already has" design as UMonsterAnimInstance's own header comment.
UCLASS()
class MYCHARACTERS_API UBaseAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	// True while the owning ABaseCharacter is crouched (ACharacter::IsCrouched(), driven by
	// ABaseCharacter::ToggleCrouch() — bound to 'C') — read every frame in NativeUpdateAnimation() below,
	// not just once, so it stays correct through Crouch()/UnCrouch()'s own one-tick delay (see
	// ToggleCrouch()'s own comment on why IsCrouched() doesn't flip synchronously). EditAnywhere (not
	// just BlueprintReadOnly) so a Blueprint AnimGraph can still preview/force a value in the editor with
	// no owning pawn possessing this instance yet.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement")
	bool bIsCrouched = false;

protected:
	virtual void NativeInitializeAnimation() override;
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;

private:
	// Cached once in NativeInitializeAnimation() via TryGetPawnOwner() — the ABaseCharacter this AnimGraph
	// is actually animating. Null (and NativeUpdateAnimation() a no-op, bIsCrouched stays at its last
	// value) for a preview/editor context with no real pawn possessing this mesh.
	UPROPERTY()
	TObjectPtr<ABaseCharacter> OwningCharacter;
};

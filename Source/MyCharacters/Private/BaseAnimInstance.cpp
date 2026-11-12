// Fill out your copyright notice in the Description page of Project Settings.

#include "BaseAnimInstance.h"
#include "BaseCharacter.h"

void UBaseAnimInstance::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();

	OwningCharacter = Cast<ABaseCharacter>(TryGetPawnOwner());
}

void UBaseAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);

	if (!IsValid(OwningCharacter))
	{
		// A pawn possession change (or none yet) since NativeInitializeAnimation() last ran — re-resolve
		// rather than staying permanently null for the rest of this AnimInstance's life.
		OwningCharacter = Cast<ABaseCharacter>(TryGetPawnOwner());
		if (!IsValid(OwningCharacter))
		{
			return;
		}
	}

	bIsCrouched = OwningCharacter->IsCrouched();
}

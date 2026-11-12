// Fill out your copyright notice in the Description page of Project Settings.

#include "MonsterAnimInstance.h"
#include "BaseMonster.h"

void UMonsterAnimInstance::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();

	OwningMonster = Cast<ABaseMonster>(TryGetPawnOwner());
}

void UMonsterAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);

	if (!IsValid(OwningMonster))
	{
		// A pawn possession change (or none yet) since NativeInitializeAnimation() last ran — re-resolve
		// rather than staying permanently null for the rest of this AnimInstance's life.
		OwningMonster = Cast<ABaseMonster>(TryGetPawnOwner());
		if (!IsValid(OwningMonster))
		{
			return;
		}
	}

	Speed = OwningMonster->GetMovementSpeed();
	bIsChasing = OwningMonster->GetIsChasing();
	bIsStalking = OwningMonster->GetIsStalking();
	bIsFleeingToShadow = OwningMonster->GetIsFleeingToShadow();
	bIsIdleInShadow = OwningMonster->GetIsIdleInShadow();
}

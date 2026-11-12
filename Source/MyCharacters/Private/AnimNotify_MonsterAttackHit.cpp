// Fill out your copyright notice in the Description page of Project Settings.

#include "AnimNotify_MonsterAttackHit.h"
#include "BaseMonster.h"
#include "Components/SkeletalMeshComponent.h"

void UAnimNotify_MonsterAttackHit::Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	Super::Notify(MeshComp, Animation, EventReference);

	if (IsValid(MeshComp))
	{
		if (ABaseMonster* Monster = Cast<ABaseMonster>(MeshComp->GetOwner()))
		{
			Monster->OnAttackHitNotify();
		}
	}
}

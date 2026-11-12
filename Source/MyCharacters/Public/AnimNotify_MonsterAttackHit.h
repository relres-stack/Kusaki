// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "AnimNotify_MonsterAttackHit.generated.h"

// Placed on Content/Whisper/Animations/Anim_Whisper_Attack1 (and any other monster attack animation
// that needs the same "contact" moment) at the point the swing's paw/claw actually reaches forward —
// NOT at the start of the animation. This is the fix for the bug where ABaseMonster::
// ResolveEyePeekReaction()'s Panic branch used to call ABaseCharacter::EnterPanicState() the INSTANT
// PlayAttackAnimation() started, regardless of how far away the player actually was — a player caught
// peeking from well outside melee range still got stunned "telepathically" the moment the swing began,
// because nothing ever re-checked distance at the moment contact was supposed to happen.
//
// Notify() below resolves the owning ABaseMonster directly from MeshComp->GetOwner() (not through
// UMonsterAnimInstance's reflection-based AnimNotify_<Name> dispatch) and calls its OnAttackHitNotify() —
// see that function's own header comment for the actual range check + stun-application logic now gated
// behind this notify instead of firing synchronously at animation start.
UCLASS(meta = (DisplayName = "Monster Attack Hit"))
class MYCHARACTERS_API UAnimNotify_MonsterAttackHit : public UAnimNotify
{
	GENERATED_BODY()

public:
	virtual FString GetNotifyName_Implementation() const override { return TEXT("Attack_Hit"); }

	virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference) override;
};

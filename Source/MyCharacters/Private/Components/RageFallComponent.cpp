// Fill out your copyright notice in the Description page of Project Settings.

#include "Components/RageFallComponent.h"
#include "BaseMonster.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimInstance.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"
#include "Engine/Engine.h"

URageFallComponent::URageFallComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void URageFallComponent::BeginPlay()
{
	Super::BeginPlay();

	if (ACharacter* OwnerCharacter = Cast<ACharacter>(GetOwner()))
	{
		if (UCapsuleComponent* Capsule = OwnerCharacter->GetCapsuleComponent())
		{
			Capsule->OnComponentHit.AddDynamic(this, &URageFallComponent::OnCapsuleHit);
		}
	}
}

void URageFallComponent::StartDashResolutionWindow()
{
	bIsAwaitingDashResult = true;
	GetWorld()->GetTimerManager().SetTimer(GraceWindowTimerHandle, this, &URageFallComponent::OnGraceWindowExpired, GraceWindow, false);

	UE_LOG(LogTemp, Warning, TEXT("%s: dash resolution window armed for %.2fs"), *GetName(), GraceWindow);
}

void URageFallComponent::OnCapsuleHit(UPrimitiveComponent* HitComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit)
{
	if (!bIsAwaitingDashResult || !IsValid(OtherActor))
	{
		// Not currently resolving a dash — ordinary walking collision, nothing for this component to do.
		return;
	}

	if (ACharacter* OwnerCharacter = Cast<ACharacter>(GetOwner()))
	{
		if (UCharacterMovementComponent* MoveComp = OwnerCharacter->GetCharacterMovement())
		{
			if (Hit.ImpactNormal.Z >= MoveComp->GetWalkableFloorZ())
			{
				// Это пол/потолок, а не стена — обычное приземление после вертикального импульса дэша,
				// не препятствие. Окно НЕ снимаем (bIsAwaitingDashResult остаётся armed) — реальное
				// столкновение со стеной, если случится следом, всё ещё должно сработать; если ничего
				// больше не случится — GraceWindow просто истечёт без последствий (как для промаха).
				return;
			}
		}
	}

	bIsAwaitingDashResult = false;
	GetWorld()->GetTimerManager().ClearTimer(GraceWindowTimerHandle);

	if (ABaseMonster* HitMonster = Cast<ABaseMonster>(OtherActor))
	{
		// Hit a monster — UMoonshineComponent::OnOwnerCapsuleHit() (its own independent subscription to
		// this same delegate) already applies the knockback/stun; this component just disarms without
		// stumbling.
		UE_LOG(LogTemp, Warning, TEXT("%s: dash hit monster %s — no stumble"), *GetName(), *HitMonster->GetName());
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("%s: dash hit obstacle %s — stumbling with damage"), *GetName(), *OtherActor->GetName());
	BeginStumble();
}

void URageFallComponent::OnGraceWindowExpired()
{
	if (!bIsAwaitingDashResult)
	{
		// Already resolved by OnCapsuleHit() before the timer fired.
		return;
	}
	bIsAwaitingDashResult = false;

	// Clean miss — deliberately no stumble/damage/message, see this class's own header comment.
	UE_LOG(LogTemp, Log, TEXT("%s: dash resolution window expired with no hit — clean miss, no consequence"), *GetName());
}

void URageFallComponent::BeginStumble()
{
	if (bIsStumbling)
	{
		return;
	}
	bIsStumbling = true;

	ACharacter* OwnerCharacter = Cast<ACharacter>(GetOwner());
	if (!IsValid(OwnerCharacter))
	{
		return;
	}

	if (UCharacterMovementComponent* MoveComp = OwnerCharacter->GetCharacterMovement())
	{
		MoveComp->DisableMovement();
	}

	UGameplayStatics::ApplyDamage(GetOwner(), WallHitDamage, nullptr, GetOwner(), nullptr);

	if (IsValid(StumbleMontage))
	{
		if (USkeletalMeshComponent* Mesh = OwnerCharacter->GetMesh())
		{
			if (UAnimInstance* AnimInstance = Mesh->GetAnimInstance())
			{
				AnimInstance->Montage_Play(StumbleMontage);
			}
		}
	}

	GetWorld()->GetTimerManager().SetTimer(StumbleTimerHandle, this, &URageFallComponent::EndStumble, StumbleDuration, false);

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(240, 2.0f, FColor::Red, TEXT("STUMBLE! (wall hit)"));
	}
}

void URageFallComponent::EndStumble()
{
	ACharacter* OwnerCharacter = Cast<ACharacter>(GetOwner());
	if (IsValid(OwnerCharacter))
	{
		if (UCharacterMovementComponent* MoveComp = OwnerCharacter->GetCharacterMovement())
		{
			MoveComp->SetMovementMode(MOVE_Walking);
		}

		if (IsValid(StumbleMontage))
		{
			if (USkeletalMeshComponent* Mesh = OwnerCharacter->GetMesh())
			{
				if (UAnimInstance* AnimInstance = Mesh->GetAnimInstance())
				{
					AnimInstance->Montage_Stop(0.2f, StumbleMontage);
				}
			}
		}
	}

	bIsStumbling = false;

	UE_LOG(LogTemp, Warning, TEXT("%s: stumble ended, movement restored"), *GetName());
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(241, 2.0f, FColor::Green, TEXT("Stumble over"));
	}
}

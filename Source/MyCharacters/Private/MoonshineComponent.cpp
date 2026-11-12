// Fill out your copyright notice in the Description page of Project Settings.

#include "MoonshineComponent.h"
#include "BaseCharacter.h"
#include "BaseMonster.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/DamageType.h"
#include "Components/CapsuleComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimSequence.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"
#include "Engine/Engine.h"

UMoonshineComponent::UMoonshineComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UMoonshineComponent::BeginPlay()
{
	Super::BeginPlay();

	if (ACharacter* OwnerCharacter = Cast<ACharacter>(GetOwner()))
	{
		if (UCapsuleComponent* Capsule = OwnerCharacter->GetCapsuleComponent())
		{
			Capsule->OnComponentHit.AddDynamic(this, &UMoonshineComponent::OnOwnerCapsuleHit);
		}
	}
}

bool UMoonshineComponent::UseMoonshine()
{
	ABaseCharacter* OwnerCharacter = Cast<ABaseCharacter>(GetOwner());
	if (!IsValid(OwnerCharacter))
	{
		return false;
	}

	if (OwnerCharacter->bIsTrapped)
	{
		UE_LOG(LogTemp, Warning, TEXT("%s: UseMoonshine() refused — trapped"), *GetName());
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(230, 2.0f, FColor::Red, TEXT("Moonshine: can't drink while trapped"));
		}
		return false;
	}

	if (!bCanDrinkMoonshine)
	{
		UE_LOG(LogTemp, Warning, TEXT("%s: UseMoonshine() refused — not ready (bIsMoonshineActive=%s)"),
			*GetName(), bIsMoonshineActive ? TEXT("true") : TEXT("false"));
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(230, 2.0f, FColor::Red, TEXT("Moonshine: not ready yet"));
		}
		return false;
	}

	// Общий конечный запас, разделяемый с НПС-приманкой (TryConsumeForNPC()) — по прямому решению
	// пользователя (27.08.2026): "самогон один и на себя и на жертву и он тратится". Правка
	// стабильного UMoonshineComponent сделана с явного разрешения пользователя в том же обсуждении.
	if (MoonshineStock < PersonalDrinkCost)
	{
		UE_LOG(LogTemp, Warning, TEXT("%s: UseMoonshine() refused — not enough stock (%.1f < %.1f)"),
			*GetName(), MoonshineStock, PersonalDrinkCost);
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(230, 2.0f, FColor::Red, TEXT("Moonshine: not enough left"));
		}
		return false;
	}

	bCanDrinkMoonshine = false;
	bIsMoonshineActive = true;
	MoonshineStock -= PersonalDrinkCost;

	// Roadmap 1.3. Forces the eyes open for the moment of drinking and locks them open for the whole
	// buff — Rule 1.1's background threat (ABaseMonster::TryStartChaseAttack() and friends) still SEES an
	// open-eyed Redneck and still reacts (chases, calls for backup via BroadcastAlert(), etc.), but the
	// damage/panic those reactions would normally land is neutralized below by
	// SetImmuneToMonsterAttacks(true) — the risk isn't "die/get knocked down mid-buff" anymore, it's
	// "every monster around now knows exactly where you are for when the buff runs out". One-shot open
	// only; the lock (not a repeated force) is what actually keeps the eyes open for the full duration.
	OwnerCharacter->ForceEyesOpen();
	OwnerCharacter->SetEyesLockedOpen(true);

	// Full immunity to monster damage/panic for the buff's duration (see SetImmuneToMonsterAttacks()'s own
	// comment) — decided alongside the roadmap 1.3 work: a monster's hit no longer knocks Redneck into the
	// Nose-Breathing panic mini-game or costs Health while the buff is up. Does NOT protect against a
	// monster's separate instant-kill paths (physical catch, Blind Encounter) — only damage/panic.
	OwnerCharacter->SetImmuneToMonsterAttacks(true);

	PreBoostSprintSpeed = OwnerCharacter->SprintSpeed;
	OwnerCharacter->SprintSpeed = PreBoostSprintSpeed * (1.0f + MoonshineSpeedBoost);

	// Auto-starts sprinting the moment the buff lands, rather than waiting for the player to hold the
	// sprint key themselves — no-ops harmlessly if already sprinting (see StartSprinting()'s own guard).
	OwnerCharacter->StartSprinting();

	// Already sprinting when the buff lands (or just started above): push the boosted value onto the live
	// MaxWalkSpeed right now, rather than leaving the character at the old speed until the next Sprint
	// toggle happens to re-read SprintSpeed.
	if (OwnerCharacter->bIsSprinting)
	{
		if (UCharacterMovementComponent* MoveComp = OwnerCharacter->GetCharacterMovement())
		{
			MoveComp->MaxWalkSpeed = OwnerCharacter->SprintSpeed;
		}
	}

	GetWorld()->GetTimerManager().SetTimer(MoonshineTimerHandle, this, &UMoonshineComponent::EndMoonshine, MoonshineDuration, false);

	UE_LOG(LogTemp, Warning, TEXT("%s: drank moonshine — SprintSpeed %.0f -> %.0f for %.1fs, ram window open"),
		*GetName(), PreBoostSprintSpeed, OwnerCharacter->SprintSpeed, MoonshineDuration);
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(231, 2.0f, FColor::Orange,
			FString::Printf(TEXT("Moonshine! Buff active for %.1fs"), MoonshineDuration));
	}

	return true;
}

void UMoonshineComponent::EndMoonshine()
{
	if (!bIsMoonshineActive)
	{
		return;
	}
	bIsMoonshineActive = false;

	if (ABaseCharacter* OwnerCharacter = Cast<ABaseCharacter>(GetOwner()))
	{
		OwnerCharacter->SetEyesLockedOpen(false);
		OwnerCharacter->SetImmuneToMonsterAttacks(false);
		OwnerCharacter->SprintSpeed = PreBoostSprintSpeed;

		if (OwnerCharacter->bIsSprinting)
		{
			if (UCharacterMovementComponent* MoveComp = OwnerCharacter->GetCharacterMovement())
			{
				MoveComp->MaxWalkSpeed = OwnerCharacter->SprintSpeed;
			}
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("%s: moonshine buff wore off — %.1fs cooldown before next drink"), *GetName(), MoonshineCooldown);

	GetWorld()->GetTimerManager().SetTimer(CooldownTimerHandle, this, &UMoonshineComponent::EndCooldown, MoonshineCooldown, false);
}

void UMoonshineComponent::PerformRageDash()
{
	if (!bIsMoonshineActive)
	{
		UE_LOG(LogTemp, Warning, TEXT("%s: PerformRageDash() refused — buff not active"), *GetName());
		return;
	}

	ACharacter* OwnerCharacter = Cast<ACharacter>(GetOwner());
	if (!IsValid(OwnerCharacter))
	{
		return;
	}

	// Flattened aim direction — same "ignore pitch, keep the dash horizontal" reasoning as
	// OnOwnerCapsuleHit()'s own KnockbackDirection below. Falls back to the character's forward vector if
	// there's no controller to read an aim rotation from (shouldn't happen for the locally-controlled
	// player this is bound for, but stay safe).
	const AController* OwnerController = OwnerCharacter->GetController();
	const FVector DashDirection = IsValid(OwnerController)
		? OwnerController->GetControlRotation().Vector().GetSafeNormal2D()
		: OwnerCharacter->GetActorForwardVector();

	const FVector LaunchVelocity = (DashDirection * DashLaunchSpeed) + FVector(0.0f, 0.0f, DashLaunchUpwardSpeed);
	OwnerCharacter->LaunchCharacter(LaunchVelocity, true, true);

	if (IsValid(RageDashAnimation))
	{
		if (USkeletalMeshComponent* MeshComp = OwnerCharacter->GetMesh())
		{
			MeshComp->PlayAnimation(RageDashAnimation, false);
			GetWorld()->GetTimerManager().SetTimer(RageDashAnimTimerHandle, this,
				&UMoonshineComponent::RestoreAnimationBlueprintControl,
				RageDashAnimation->GetPlayLength(), false);
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("%s: Rage Dash launched toward %s at %.0f units/sec"),
		*GetName(), *DashDirection.ToCompactString(), DashLaunchSpeed);
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(235, 2.0f, FColor::Cyan, TEXT("RAGE DASH!"));
	}
}

void UMoonshineComponent::RestoreAnimationBlueprintControl()
{
	if (ACharacter* OwnerCharacter = Cast<ACharacter>(GetOwner()))
	{
		if (USkeletalMeshComponent* MeshComp = OwnerCharacter->GetMesh())
		{
			MeshComp->SetAnimationMode(EAnimationMode::AnimationBlueprint);
		}
	}
}

void UMoonshineComponent::EndCooldown()
{
	bCanDrinkMoonshine = true;

	UE_LOG(LogTemp, Warning, TEXT("%s: moonshine ready again"), *GetName());
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(232, 2.0f, FColor::Green, TEXT("Moonshine ready"));
	}
}

void UMoonshineComponent::OnOwnerCapsuleHit(UPrimitiveComponent* HitComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit)
{
	if (!bIsMoonshineActive || !IsValid(OtherActor))
	{
		return;
	}

	ACharacter* OwnerCharacter = Cast<ACharacter>(GetOwner());
	if (!IsValid(OwnerCharacter))
	{
		return;
	}

	// Rage Dash landed: ram whatever was hit instead of just letting the capsule collision block the
	// player's movement like an ordinary bump would.
	const FVector KnockbackDirection = (OtherActor->GetActorLocation() - OwnerCharacter->GetActorLocation()).GetSafeNormal2D();

	if (ABaseMonster* HitMonster = Cast<ABaseMonster>(OtherActor))
	{
		HitMonster->ApplyKnockback(KnockbackDirection, RamKnockbackForce, RamKnockbackUpwardForce);
		HitMonster->Stun(RamStunDuration);

		UE_LOG(LogTemp, Warning, TEXT("%s: Rage Dash rammed monster %s"), *GetName(), *HitMonster->GetName());
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(233, 2.0f, FColor::Yellow,
				FString::Printf(TEXT("Rage Dash rammed %s!"), *HitMonster->GetName()));
		}
		return;
	}

	// Not a monster — a destructible/obstacle actor (doors, breakable props, etc.). No dedicated
	// destructible class exists in this project yet, so this is deliberately generic: ApplyDamage() is
	// what a future Door/obstacle actor's own TakeDamage() override would react to, and a direct physics
	// impulse covers anything already simulating physics (a hinged door, a shove-able prop) even before
	// any TakeDamage() override exists. Harmless no-op against plain static geometry (a wall/floor) —
	// ApplyDamage() on an actor with no TakeDamage() override just does nothing, and the physics-impulse
	// branch below is skipped entirely since static geometry never reports IsSimulatingPhysics().
	UGameplayStatics::ApplyDamage(OtherActor, RamDamage, OwnerCharacter->GetInstigatorController(), OwnerCharacter, UDamageType::StaticClass());

	if (IsValid(OtherComp) && OtherComp->IsSimulatingPhysics())
	{
		OtherComp->AddImpulseAtLocation(KnockbackDirection * RamKnockbackForce, Hit.ImpactPoint);
	}

	UE_LOG(LogTemp, Warning, TEXT("%s: Rage Dash rammed obstacle %s (Damage=%.0f)"), *GetName(), *OtherActor->GetName(), RamDamage);
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(234, 2.0f, FColor::Yellow,
			FString::Printf(TEXT("Rage Dash rammed %s!"), *OtherActor->GetName()));
	}
}

bool UMoonshineComponent::TryConsumeForNPC(float MinAmount, float MaxAmount, float& OutAmountConsumed)
{
	OutAmountConsumed = 0.0f;
	if (MoonshineStock <= 0.0f)
	{
		return false;
	}

	const float CappedMax = FMath::Min(MaxAmount, MoonshineStock);
	const float ActualMin = FMath::Min(MinAmount, CappedMax);
	OutAmountConsumed = FMath::FRandRange(ActualMin, CappedMax);
	MoonshineStock -= OutAmountConsumed;

	UE_LOG(LogTemp, Warning, TEXT("%s: TryConsumeForNPC() poured %.1f units (stock now %.1f)"),
		*GetName(), OutAmountConsumed, MoonshineStock);
	return true;
}

float UMoonshineComponent::GetMoonshineFillFraction() const
{
	if (!GetWorld())
	{
		return 1.0f;
	}

	if (bIsMoonshineActive)
	{
		if (MoonshineDuration <= 0.0f)
		{
			return 0.0f;
		}
		const float TimeRemaining = GetWorld()->GetTimerManager().GetTimerRemaining(MoonshineTimerHandle);
		return FMath::Clamp(TimeRemaining / MoonshineDuration, 0.0f, 1.0f);
	}

	if (!bCanDrinkMoonshine)
	{
		// Mid-cooldown: climb 0 -> 1 as MoonshineCooldown counts down (a standard "ability on cooldown"
		// refill readout), rather than sitting pinned at a flat 0 for the whole window — the HUD's fill
		// bar (see UPlayerHUDWidget::PaintQuickSlot()) only draws anything above 0, so this is what makes
		// the cooldown's progress actually visible instead of just "not ready" with no further detail.
		if (MoonshineCooldown <= 0.0f)
		{
			return 1.0f;
		}
		const float TimeRemaining = GetWorld()->GetTimerManager().GetTimerRemaining(CooldownTimerHandle);
		return 1.0f - FMath::Clamp(TimeRemaining / MoonshineCooldown, 0.0f, 1.0f);
	}

	return 1.0f;
}

float UMoonshineComponent::GetMoonshineSecondsRemaining() const
{
	if (!GetWorld())
	{
		return 0.0f;
	}

	if (bIsMoonshineActive)
	{
		return FMath::Max(0.0f, GetWorld()->GetTimerManager().GetTimerRemaining(MoonshineTimerHandle));
	}

	if (!bCanDrinkMoonshine)
	{
		return FMath::Max(0.0f, GetWorld()->GetTimerManager().GetTimerRemaining(CooldownTimerHandle));
	}

	return 0.0f;
}

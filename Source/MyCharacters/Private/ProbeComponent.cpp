// Fill out your copyright notice in the Description page of Project Settings.

#include "ProbeComponent.h"
#include "BaseCharacter.h"
#include "BaseMonster.h"
#include "Engine/Engine.h"
#include "CollisionQueryParams.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"

UProbeComponent::UProbeComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UProbeComponent::Probe()
{
	ABaseCharacter* OwnerCharacter = Cast<ABaseCharacter>(GetOwner());
	if (!IsValid(OwnerCharacter))
	{
		return;
	}

	// Только пока присел ("крадётся") — по прямому решению пользователя. Остальные guard'ы — та же
	// "нельзя действовать, пока недееспособен" логика, что и у других способностей в проекте.
	if (!OwnerCharacter->IsCrouched() || OwnerCharacter->bIsPanicking || OwnerCharacter->bIsTrapped || OwnerCharacter->bIsDead)
	{
		return;
	}

	// Горизонтальный трейс от ТЕЛА персонажа (GetActorLocation() — центр капсулы, автоматически на
	// разумной высоте что стоя, что в приседе — капсула сама пересчитывает центр), строго по
	// GetActorForwardVector(), а НЕ от камеры (в отличие от TraceHunterFocusTarget()/TraceAimTarget()) —
	// прощупывание вслепую не должно зависеть от текущего наклона камеры игрока, иначе луч утыкается в
	// пол на первых же юнитах вместо честной горизонтальной проверки "что прямо передо мной".
	const FVector TraceStart = OwnerCharacter->GetActorLocation();
	const FVector TraceEnd = TraceStart + OwnerCharacter->GetActorForwardVector() * ProbeRange;

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(ProbeTrace), false);
	QueryParams.AddIgnoredActor(OwnerCharacter);
	if (OwnerCharacter->GetMesh())
	{
		QueryParams.AddIgnoredComponent(OwnerCharacter->GetMesh());
	}
	if (OwnerCharacter->GetCapsuleComponent())
	{
		QueryParams.AddIgnoredComponent(OwnerCharacter->GetCapsuleComponent());
	}

	FHitResult OutHit;
	const bool bHit = GetWorld()->LineTraceSingleByChannel(OutHit, TraceStart, TraceEnd, ECC_Visibility, QueryParams);

	if (!bHit)
	{
		UE_LOG(LogTemp, Warning, TEXT("%s: Probe() — clear ahead"), *OwnerCharacter->GetName());
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(270, 2.5f, FColor::Green, TEXT("Впереди свободно"));
		}
		return;
	}

	if (ABaseMonster* Monster = Cast<ABaseMonster>(OutHit.GetActor()))
	{
		if (Monster->IsScanningForPlayer(OwnerCharacter) || Monster->IsStartledByProbe(OwnerCharacter))
		{
			UE_LOG(LogTemp, Warning, TEXT("%s: Probe() — %s is already alert to us, provoked!"),
				*OwnerCharacter->GetName(), *Monster->GetName());
			if (GEngine)
			{
				GEngine->AddOnScreenDebugMessage(271, 2.5f, FColor::Red, TEXT("Рычание... он тебя услышал!"));
			}
			Monster->PunishRepeatProvocation(OwnerCharacter);
			return;
		}

		UE_LOG(LogTemp, Warning, TEXT("%s: Probe() — %s ahead, safe read"), *OwnerCharacter->GetName(), *Monster->GetName());
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(272, 2.5f, FColor::Orange, TEXT("Рычание... впереди монстр — туда нельзя"));
		}
		Monster->ReactToProbe(OwnerCharacter);
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("%s: Probe() — solid obstacle ahead"), *OwnerCharacter->GetName());
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(273, 2.5f, FColor::Yellow, TEXT("Похоже, впереди стена"));
	}
}

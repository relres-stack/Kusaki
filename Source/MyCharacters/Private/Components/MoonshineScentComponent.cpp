// Fill out your copyright notice in the Description page of Project Settings.

#include "Components/MoonshineScentComponent.h"
#include "BaseCharacter.h"
#include "BaseMonster.h"
#include "RedneckCharacter.h"
#include "MoonshineComponent.h"
#include "TimerManager.h"
#include "Engine/Engine.h"

UMoonshineScentComponent::UMoonshineScentComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UMoonshineScentComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	ARedneckCharacter* OwnerCharacter = Cast<ARedneckCharacter>(GetOwner());
	if (!IsValid(OwnerCharacter) || !IsValid(OwnerCharacter->MoonshineComponent))
	{
		return;
	}

	const bool bIsMoonshineActiveNow = OwnerCharacter->MoonshineComponent->bIsMoonshineActive;

	if (bWasMoonshineActiveLastTick && !bIsMoonshineActiveNow)
	{
		// Buff just wore off — see this class's own header comment on why the scent trail starts HERE,
		// not from the dash itself.
		UE_LOG(LogTemp, Warning, TEXT("%s: moonshine buff wore off — starting hangover scent trail"), *GetName());
		StartScentTrail();
	}

	bWasMoonshineActiveLastTick = bIsMoonshineActiveNow;
}

void UMoonshineScentComponent::StartScentTrail()
{
	GetWorld()->GetTimerManager().SetTimer(ScentPulseTimerHandle, this, &UMoonshineScentComponent::OnScentPulse, ScentPulseInterval, true);
	GetWorld()->GetTimerManager().SetTimer(ScentDurationTimerHandle, this, &UMoonshineScentComponent::StopScentTrail, ScentDuration, false);

	UE_LOG(LogTemp, Warning, TEXT("%s: scent trail armed — pulsing every %.2fs for %.1fs"), *GetName(), ScentPulseInterval, ScentDuration);
}

void UMoonshineScentComponent::OnScentPulse()
{
	ABaseCharacter* OwnerCharacter = Cast<ABaseCharacter>(GetOwner());
	if (!IsValid(OwnerCharacter))
	{
		return;
	}

	ABaseMonster* NearbyMonster = OwnerCharacter->FindNearbyMonster(ScentSearchRadius);
	if (!IsValid(NearbyMonster))
	{
		UE_LOG(LogTemp, Warning, TEXT("%s: scent pulse — nothing within %.0f units"), *GetName(), ScentSearchRadius);
		return;
	}

	NearbyMonster->BroadcastAlert(OwnerCharacter->GetActorLocation());

	UE_LOG(LogTemp, Warning, TEXT("%s: scent pulse — %s caught the scent, broadcasting alert"), *GetName(), *NearbyMonster->GetName());
}

void UMoonshineScentComponent::StopScentTrail()
{
	GetWorld()->GetTimerManager().ClearTimer(ScentPulseTimerHandle);

	UE_LOG(LogTemp, Warning, TEXT("%s: scent trail faded"), *GetName());
}

// Fill out your copyright notice in the Description page of Project Settings.

#include "FingerFlamePeekComponent.h"
#include "BaseCharacter.h"
#include "Components/PointLightComponent.h"
#include "Components/SilhouetteVisionComponent.h"

UFingerFlamePeekComponent::UFingerFlamePeekComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UFingerFlamePeekComponent::TogglePeek()
{
	if (bIsPeekActive)
	{
		DeactivatePeek();
		return;
	}

	if (!bCanUsePeek)
	{
		UE_LOG(LogTemp, Warning, TEXT("%s: TogglePeek() refused — still on cooldown"), *GetName());
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(262, 2.0f, FColor::Red, TEXT("Finger Flame: on cooldown"));
		}
		return;
	}

	const ABaseCharacter* OwnerCharacter = Cast<ABaseCharacter>(GetOwner());
	if (!IsValid(OwnerCharacter) || !OwnerCharacter->IsEyesClosed())
	{
		UE_LOG(LogTemp, Warning, TEXT("%s: TogglePeek() refused — eyes are open"), *GetName());
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(263, 2.0f, FColor::Yellow, TEXT("Finger Flame: only works with eyes closed"));
		}
		return;
	}

	ActivatePeek();
}

void UFingerFlamePeekComponent::ActivatePeek()
{
	bIsPeekActive = true;

	if (IsValid(LightComponent))
	{
		LightComponent->SetVisibility(true);
	}

	if (IsValid(VisionComponent))
	{
		VisionComponent->ActivateVision();
	}

	UE_LOG(LogTemp, Warning, TEXT("%s: Finger Flame peek started — %.1fs, eyes stay closed the whole time"), *GetName(), PeekDuration);
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(264, 2.0f, FColor::Orange,
			FString::Printf(TEXT("Finger Flame: peeking (%.0fs)"), PeekDuration));
	}

	GetWorld()->GetTimerManager().SetTimer(PeekDurationTimerHandle, this, &UFingerFlamePeekComponent::DeactivatePeek, PeekDuration, false);
}

void UFingerFlamePeekComponent::DeactivatePeek()
{
	if (!bIsPeekActive)
	{
		return;
	}

	// Still running means this is a manual TogglePeek() cutting the peek short — the natural PeekDuration
	// timeout, by contrast, has already fired by the time it calls this, so the handle it fired from is no
	// longer "active". Purely cosmetic (which debug line prints); the cleanup/cooldown below is identical
	// either way.
	const bool bEndedEarly = GetWorld()->GetTimerManager().IsTimerActive(PeekDurationTimerHandle);

	bIsPeekActive = false;
	GetWorld()->GetTimerManager().ClearTimer(PeekDurationTimerHandle);

	if (IsValid(LightComponent))
	{
		LightComponent->SetVisibility(false);
	}

	if (IsValid(VisionComponent))
	{
		VisionComponent->DeactivateVision();
	}

	UE_LOG(LogTemp, Warning, TEXT("%s: Finger Flame peek %s — %.1fs cooldown starting"),
		*GetName(), bEndedEarly ? TEXT("cancelled early") : TEXT("ended"), PeekCooldown);
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(265, 2.0f, FColor::Silver,
			bEndedEarly ? TEXT("Finger Flame: cancelled") : TEXT("Finger Flame: peek ended"));
	}

	bCanUsePeek = false;
	GetWorld()->GetTimerManager().SetTimer(CooldownTimerHandle, this, &UFingerFlamePeekComponent::EndCooldown, PeekCooldown, false);
}

void UFingerFlamePeekComponent::EndCooldown()
{
	bCanUsePeek = true;

	UE_LOG(LogTemp, Warning, TEXT("%s: Finger Flame ready again"), *GetName());
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(266, 2.0f, FColor::Green, TEXT("Finger Flame: ready"));
	}
}

void UFingerFlamePeekComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(PeekDurationTimerHandle);
		World->GetTimerManager().ClearTimer(CooldownTimerHandle);
	}

	Super::EndPlay(EndPlayReason);
}

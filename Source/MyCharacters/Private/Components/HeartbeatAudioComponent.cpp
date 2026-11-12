// Fill out your copyright notice in the Description page of Project Settings.

#include "Components/HeartbeatAudioComponent.h"
#include "BaseCharacter.h"
#include "Components/AudioComponent.h"

UHeartbeatAudioComponent::UHeartbeatAudioComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UHeartbeatAudioComponent::BeginPlay()
{
	Super::BeginPlay();

	OwnerCharacter = Cast<ABaseCharacter>(GetOwner());
	if (!OwnerCharacter.IsValid())
	{
		return;
	}

	USceneComponent* RootComp = OwnerCharacter->GetRootComponent();

	if (CalmHeartbeatSound)
	{
		CalmHeartbeatComp = NewObject<UAudioComponent>(OwnerCharacter.Get(), TEXT("CalmHeartbeatComp"));
		CalmHeartbeatComp->SetupAttachment(RootComp);
		CalmHeartbeatComp->RegisterComponent();
		CalmHeartbeatComp->bAllowSpatialization = false;
		CalmHeartbeatComp->SetSound(CalmHeartbeatSound);
	}

	if (CalmBreathingSound)
	{
		CalmBreathingComp = NewObject<UAudioComponent>(OwnerCharacter.Get(), TEXT("CalmBreathingComp"));
		CalmBreathingComp->SetupAttachment(RootComp);
		CalmBreathingComp->RegisterComponent();
		CalmBreathingComp->bAllowSpatialization = false;
		CalmBreathingComp->SetSound(CalmBreathingSound);
	}

	if (PanicHeartbeatSound)
	{
		PanicHeartbeatComp = NewObject<UAudioComponent>(OwnerCharacter.Get(), TEXT("PanicHeartbeatComp"));
		PanicHeartbeatComp->SetupAttachment(RootComp);
		PanicHeartbeatComp->RegisterComponent();
		PanicHeartbeatComp->bAllowSpatialization = false;
		PanicHeartbeatComp->SetSound(PanicHeartbeatSound);
	}

	// Starting state is always "not panicking" — start the calm loop(s) immediately if assigned.
	if (IsValid(CalmHeartbeatComp))
	{
		CalmHeartbeatComp->Play();
	}
	if (IsValid(CalmBreathingComp))
	{
		CalmBreathingComp->Play();
	}
}

void UHeartbeatAudioComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	ABaseCharacter* Character = OwnerCharacter.Get();
	if (!IsValid(Character))
	{
		return;
	}

	const bool bIsPanickingNow = Character->bIsPanicking;
	if (bIsPanickingNow == bWasPanickingLastTick)
	{
		return;
	}
	bWasPanickingLastTick = bIsPanickingNow;

	if (bIsPanickingNow)
	{
		if (IsValid(CalmHeartbeatComp))
		{
			CalmHeartbeatComp->Stop();
		}
		if (IsValid(CalmBreathingComp))
		{
			CalmBreathingComp->Stop();
		}
		if (IsValid(PanicHeartbeatComp))
		{
			PanicHeartbeatComp->Play();
		}
	}
	else
	{
		if (IsValid(PanicHeartbeatComp))
		{
			PanicHeartbeatComp->Stop();
		}
		if (IsValid(CalmHeartbeatComp))
		{
			CalmHeartbeatComp->Play();
		}
		if (IsValid(CalmBreathingComp))
		{
			CalmBreathingComp->Play();
		}
	}
}

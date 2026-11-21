// Fill out your copyright notice in the Description page of Project Settings.

#include "Components/HeartbeatAudioComponent.h"
#include "BaseCharacter.h"
#include "Components/AudioComponent.h"
#include "GameFramework/PlayerController.h"

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

	// Nothing is started here: the first TickComponent() picks the right state. At BeginPlay() the player's
	// starting hero is usually not possessed yet, so starting the calm loop here would be wrong for an
	// AI-controlled hero (see AppliedState's own comment).
}

void UHeartbeatAudioComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	ABaseCharacter* Character = OwnerCharacter.Get();
	if (!IsValid(Character))
	{
		return;
	}

	EHeartbeatState Wanted = EHeartbeatState::Silent;
	if (IsValid(Cast<APlayerController>(Character->GetController())))
	{
		Wanted = Character->bIsPanicking ? EHeartbeatState::Panic : EHeartbeatState::Calm;
	}
	if (Wanted != AppliedState)
	{
		ApplyState(Wanted);
	}
}

void UHeartbeatAudioComponent::ApplyState(EHeartbeatState NewState)
{
	AppliedState = NewState;

	const bool bCalm = (NewState == EHeartbeatState::Calm);
	const bool bPanic = (NewState == EHeartbeatState::Panic);

	// Stop first, then start, so a calm -> panic change never has both playing for a frame.
	if (IsValid(CalmHeartbeatComp) && !bCalm)
	{
		CalmHeartbeatComp->Stop();
	}
	if (IsValid(CalmBreathingComp) && !bCalm)
	{
		CalmBreathingComp->Stop();
	}
	if (IsValid(PanicHeartbeatComp) && !bPanic)
	{
		PanicHeartbeatComp->Stop();
	}

	if (bCalm)
	{
		if (IsValid(CalmHeartbeatComp))
		{
			CalmHeartbeatComp->Play();
		}
		if (IsValid(CalmBreathingComp))
		{
			CalmBreathingComp->Play();
		}
	}
	if (bPanic && IsValid(PanicHeartbeatComp))
	{
		PanicHeartbeatComp->Play();
	}
}

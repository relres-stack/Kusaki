// Fill out your copyright notice in the Description page of Project Settings.

#include "Components/InkVisionComponent.h"
#include "BaseCharacter.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Engine/PostProcessVolume.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "TimerManager.h"

UInkVisionComponent::UInkVisionComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UInkVisionComponent::BeginPlay()
{
	Super::BeginPlay();

	// Wrap the assigned material in our OWN runtime Dynamic Material Instance and add THAT to the level's
	// Post Process Volume — same pattern as UVisionFireSourceComponent::BeginPlay(). No-op (leaves
	// InkVisionMID null) if InkVisionMaterial was left unset or no volume exists yet.
	if (IsValid(InkVisionMaterial) && GetWorld())
	{
		InkVisionMID = UMaterialInstanceDynamic::Create(InkVisionMaterial, this);
		if (IsValid(InkVisionMID))
		{
			InkVisionMID->SetScalarParameterValue(TEXT("VisionEffectStrength"), 0.0f);

			for (TActorIterator<APostProcessVolume> It(GetWorld()); It; ++It)
			{
				It->Settings.AddBlendable(InkVisionMID, 1.0f);
				break;
			}
		}
	}
}

void UInkVisionComponent::ToggleVision()
{
	if (bIsVisionActive)
	{
		EndVisionEffect();
		return;
	}

	if (!bCanUseVision)
	{
		UE_LOG(LogTemp, Warning, TEXT("%s: ToggleVision() refused — still on cooldown"), *GetOwner()->GetName());
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 2.0f, FColor::Red, TEXT("Чернильное зрение: на перезарядке"));
		}
		return;
	}

	bIsVisionActive = true;

	if (IsValid(InkVisionMID))
	{
		InkVisionMID->SetScalarParameterValue(TEXT("VisionEffectStrength"), 1.0f);
	}

	if (ABaseCharacter* OwnerCharacter = Cast<ABaseCharacter>(GetOwner()))
	{
		OwnerCharacter->SetBlindedByAbility(true);
		OwnerCharacter->SetEyesClosedDarknessSuppressed(true);
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(VisionDurationTimerHandle, this, &UInkVisionComponent::EndVisionEffect, VisionDuration, false);
	}
}

void UInkVisionComponent::EndVisionEffect()
{
	if (!bIsVisionActive)
	{
		return;
	}

	bIsVisionActive = false;

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(VisionDurationTimerHandle);
	}

	if (IsValid(InkVisionMID))
	{
		InkVisionMID->SetScalarParameterValue(TEXT("VisionEffectStrength"), 0.0f);
	}

	if (ABaseCharacter* OwnerCharacter = Cast<ABaseCharacter>(GetOwner()))
	{
		OwnerCharacter->SetBlindedByAbility(false);
		OwnerCharacter->SetEyesClosedDarknessSuppressed(false);
	}

	bCanUseVision = false;
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(VisionCooldownTimerHandle, this, &UInkVisionComponent::EndVisionCooldown, VisionCooldown, false);
	}
}

void UInkVisionComponent::EndVisionCooldown()
{
	bCanUseVision = true;
}

void UInkVisionComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(VisionDurationTimerHandle);
		World->GetTimerManager().ClearTimer(VisionCooldownTimerHandle);
	}
	Super::EndPlay(EndPlayReason);
}

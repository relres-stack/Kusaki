// Fill out your copyright notice in the Description page of Project Settings.

#include "Components/RiderHookComponent.h"
#include "RiderCharacter.h"
#include "HookPoint.h"
#include "MountRiderComponent.h"
#include "Components/HidingComponent.h"
#include "Engine/Engine.h"
#include "GameFramework/CharacterMovementComponent.h"

URiderHookComponent::URiderHookComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void URiderHookComponent::BeginPlay()
{
	Super::BeginPlay();

	OwnerCharacter = Cast<ARiderCharacter>(GetOwner());
}

void URiderHookComponent::TryGrapple(AHookPoint* Hook)
{
	if (!IsValid(OwnerCharacter) || !IsValid(Hook))
	{
		return;
	}

	const float Distance = FVector::Dist(OwnerCharacter->GetActorLocation(), Hook->GetActorLocation());
	if (Distance > HookGrappleRange)
	{
		UE_LOG(LogTemp, Warning, TEXT("TryGrapple: %s too far from %s (%.0f > %.0f)"),
			*OwnerCharacter->GetName(), *Hook->GetName(), Distance, HookGrappleRange);
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(292, 2.0f, FColor::Orange,
				FString::Printf(TEXT("Hook too far: %.0f / %.0f"), Distance, HookGrappleRange));
		}
		return;
	}

	if (IsValid(OwnerCharacter->MountRiderComponent))
	{
		OwnerCharacter->MountRiderComponent->Dismount();
	}

	OwnerCharacter->SetActorLocationAndRotation(Hook->GetActorLocation(), OwnerCharacter->GetActorRotation());

	if (UCharacterMovementComponent* MoveComp = OwnerCharacter->GetCharacterMovement())
	{
		MoveComp->DisableMovement();
	}
	OwnerCharacter->SetActorEnableCollision(false);

	if (UHidingComponent* Hiding = OwnerCharacter->FindComponentByClass<UHidingComponent>())
	{
		Hiding->SetHiddenExternally(true);
	}

	bIsHanging = true;
	CurrentHook = Hook;

	UE_LOG(LogTemp, Warning, TEXT("%s: grappled onto %s"), *OwnerCharacter->GetName(), *Hook->GetName());
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(290, 2.5f, FColor::Cyan, TEXT("Hanging on hook — press F to drop"));
	}
}

void URiderHookComponent::DropFromHook()
{
	if (!IsValid(OwnerCharacter))
	{
		return;
	}

	OwnerCharacter->SetActorEnableCollision(true);
	if (UCharacterMovementComponent* MoveComp = OwnerCharacter->GetCharacterMovement())
	{
		MoveComp->SetMovementMode(MOVE_Falling);
	}

	if (UHidingComponent* Hiding = OwnerCharacter->FindComponentByClass<UHidingComponent>())
	{
		Hiding->SetHiddenExternally(false);
	}

	bIsHanging = false;
	CurrentHook = nullptr;

	UE_LOG(LogTemp, Warning, TEXT("%s: dropped from hook"), *OwnerCharacter->GetName());
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(291, 2.5f, FColor::Cyan, TEXT("Dropped from hook"));
	}
}

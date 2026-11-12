// Fill out your copyright notice in the Description page of Project Settings.

#include "Components/HidingComponent.h"
#include "BaseCharacter.h"
#include "HidingSpot.h"
#include "Engine/Engine.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"

UHidingComponent::UHidingComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UHidingComponent::TryToggle()
{
	ABaseCharacter* OwnerCharacter = Cast<ABaseCharacter>(GetOwner());
	if (!IsValid(OwnerCharacter))
	{
		return;
	}

	if (bIsHidden)
	{
		ExitHidingSpot();
		return;
	}

	// Same incapacitation reasons UMountRiderComponent::CanMountMonster() already checks for mounting,
	// applied here instead — IsMounted() is a harmless "always false" on a hero that can't mount at all.
	if (OwnerCharacter->bIsTrapped || OwnerCharacter->bIsPanicking || OwnerCharacter->bIsAwaitingGetUp || OwnerCharacter->IsMounted())
	{
		return;
	}

	TArray<AActor*> Spots;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), AHidingSpot::StaticClass(), Spots);

	AHidingSpot* NearestSpot = nullptr;
	float NearestDistance = HidingSpotSearchRadius;
	for (AActor* SpotActor : Spots)
	{
		AHidingSpot* Spot = Cast<AHidingSpot>(SpotActor);
		if (!IsValid(Spot) || Spot->IsOccupied())
		{
			continue;
		}

		const float Distance = FVector::Dist(OwnerCharacter->GetActorLocation(), Spot->GetActorLocation());
		if (Distance <= NearestDistance)
		{
			NearestDistance = Distance;
			NearestSpot = Spot;
		}
	}

	if (IsValid(NearestSpot))
	{
		EnterHidingSpot(NearestSpot);
	}
}

void UHidingComponent::EnterHidingSpot(AHidingSpot* Spot)
{
	ABaseCharacter* OwnerCharacter = Cast<ABaseCharacter>(GetOwner());
	if (!IsValid(OwnerCharacter) || !IsValid(Spot))
	{
		return;
	}

	PreHideLocation = OwnerCharacter->GetActorLocation();
	PreHideRotation = OwnerCharacter->GetActorRotation();

	Spot->SetOccupied(true);
	OwnerCharacter->SetActorLocationAndRotation(Spot->GetActorLocation(), OwnerCharacter->GetActorRotation());

	if (UCharacterMovementComponent* MoveComp = OwnerCharacter->GetCharacterMovement())
	{
		MoveComp->DisableMovement();
	}
	// Also guarantees a monster can never physically touch this capsule while hidden — see this class's
	// own header comment.
	OwnerCharacter->SetActorEnableCollision(false);

	bIsHidden = true;
	CurrentHidingSpot = Spot;

	UE_LOG(LogTemp, Warning, TEXT("%s: hid in %s"), *OwnerCharacter->GetName(), *Spot->GetName());
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(280, 2.5f, FColor::Cyan, TEXT("Hidden — press E to come out"));
	}
}

void UHidingComponent::ExitHidingSpot()
{
	ABaseCharacter* OwnerCharacter = Cast<ABaseCharacter>(GetOwner());
	if (!IsValid(OwnerCharacter))
	{
		return;
	}

	OwnerCharacter->SetActorLocationAndRotation(PreHideLocation, PreHideRotation);

	OwnerCharacter->SetActorEnableCollision(true);
	if (UCharacterMovementComponent* MoveComp = OwnerCharacter->GetCharacterMovement())
	{
		MoveComp->SetMovementMode(MOVE_Walking);
	}

	if (CurrentHidingSpot.IsValid())
	{
		CurrentHidingSpot->SetOccupied(false);
	}

	bIsHidden = false;
	CurrentHidingSpot = nullptr;

	UE_LOG(LogTemp, Warning, TEXT("%s: exited hiding spot"), *OwnerCharacter->GetName());
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(281, 2.5f, FColor::Cyan, TEXT("Out of hiding"));
	}
}

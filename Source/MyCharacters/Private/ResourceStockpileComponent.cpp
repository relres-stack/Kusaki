// Fill out your copyright notice in the Description page of Project Settings.

#include "ResourceStockpileComponent.h"

UResourceStockpileComponent::UResourceStockpileComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

bool UResourceStockpileComponent::TrySpend(int32 Amount)
{
	if (Amount <= 0)
	{
		return false;
	}

	if (Amount > CurrentAmount)
	{
		return false;
	}

	CurrentAmount -= Amount;
	return true;
}

int32 UResourceStockpileComponent::Add(int32 Amount)
{
	if (Amount <= 0)
	{
		return 0;
	}

	const int32 ActuallyAdded = FMath::Min(Amount, MaxAmount - CurrentAmount);
	CurrentAmount += ActuallyAdded;
	return ActuallyAdded;
}

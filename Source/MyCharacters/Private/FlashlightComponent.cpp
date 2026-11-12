// Fill out your copyright notice in the Description page of Project Settings.

#include "FlashlightComponent.h"
#include "Components/SpotLightComponent.h"

UFlashlightComponent::UFlashlightComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

bool UFlashlightComponent::IsOn() const
{
	return IsValid(LightComponent) && LightComponent->IsVisible();
}

void UFlashlightComponent::Toggle()
{
	if (!IsValid(LightComponent))
	{
		return;
	}

	if (!LightComponent->IsVisible() && BatteryRemaining <= 0.0f)
	{
		// Refuses to turn on with a dead battery rather than flicking on a light that'll immediately
		// auto-off again the next Tick().
		UE_LOG(LogTemp, Warning, TEXT("%s: flashlight battery dead — can't turn on"), *GetName());
		return;
	}

	LightComponent->SetVisibility(!LightComponent->IsVisible());
}

void UFlashlightComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!IsValid(LightComponent) || !LightComponent->IsVisible() || BatteryDrainRate <= 0.0f || BatteryRemaining <= 0.0f)
	{
		return;
	}

	BatteryRemaining = FMath::Max(0.0f, BatteryRemaining - BatteryDrainRate * DeltaTime);
	if (BatteryRemaining <= 0.0f)
	{
		UE_LOG(LogTemp, Warning, TEXT("%s: flashlight battery died — turning off"), *GetName());
		LightComponent->SetVisibility(false);
	}
}

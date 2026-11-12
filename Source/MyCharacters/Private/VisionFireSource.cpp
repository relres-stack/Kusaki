// Fill out your copyright notice in the Description page of Project Settings.

#include "VisionFireSource.h"
#include "Components/PointLightComponent.h"
#include "NiagaraComponent.h"

AVisionFireSource::AVisionFireSource()
{
	PrimaryActorTick.bCanEverTick = false;

	USceneComponent* SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	// Movable is required on the whole chain — this actor gets SetActorLocation()'d every tick while the
	// ability is active (see UVisionFireSourceComponent::UpdateFirePlacement()). A Static-mobility light
	// still LOOKS like it turns on, but its rendered position/lighting stays pinned near wherever it was
	// when lighting was last built (effectively spawn location) instead of tracking the moved transform —
	// symptom: the fire visibly ends up back at/near the owner instead of out at the traced spot, and its
	// color reads wrong because it's falling back to unbuilt-static-lighting behavior instead of a normal
	// real-time dynamic light.
	SceneRoot->SetMobility(EComponentMobility::Movable);
	RootComponent = SceneRoot;

	FireLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("FireLight"));
	FireLight->SetMobility(EComponentMobility::Movable);
	FireLight->SetupAttachment(RootComponent);
	FireLight->SetIntensity(6500.0f);
	FireLight->SetAttenuationRadius(400.0f);
	FireLight->SetLightColor(FLinearColor(1.0f, 0.45f, 0.15f));
	FireLight->SetVisibility(false);

	// No Niagara System asset assigned here — a Blueprint subclass assigns the actual fire VFX in its
	// Details panel, same precedent as AHolyItem::FlameParticleComponent.
	FireNiagaraComponent = CreateDefaultSubobject<UNiagaraComponent>(TEXT("FireNiagaraComponent"));
	FireNiagaraComponent->SetMobility(EComponentMobility::Movable);
	FireNiagaraComponent->SetupAttachment(RootComponent);
	FireNiagaraComponent->SetVisibility(false);
	FireNiagaraComponent->bAutoActivate = false;

	// No Niagara System asset assigned here either — same reasoning as FireNiagaraComponent above, assign
	// an actual smoke effect on a BP child (design doc Step 8, п.7).
	SmokeNiagaraComponent = CreateDefaultSubobject<UNiagaraComponent>(TEXT("SmokeNiagaraComponent"));
	SmokeNiagaraComponent->SetMobility(EComponentMobility::Movable);
	SmokeNiagaraComponent->SetupAttachment(RootComponent);
	SmokeNiagaraComponent->SetVisibility(false);
	SmokeNiagaraComponent->bAutoActivate = false;

	SetActorHiddenInGame(true);
}

void AVisionFireSource::BeginPlay()
{
	Super::BeginPlay();
}

void AVisionFireSource::Activate()
{
	SetActorHiddenInGame(false);

	if (IsValid(FireLight))
	{
		FireLight->SetVisibility(true);
	}
	if (IsValid(FireNiagaraComponent))
	{
		FireNiagaraComponent->SetVisibility(true);
		FireNiagaraComponent->Activate(true);
	}
	if (IsValid(SmokeNiagaraComponent))
	{
		SmokeNiagaraComponent->SetVisibility(true);
		SmokeNiagaraComponent->Activate(true);
	}
}

void AVisionFireSource::Deactivate()
{
	SetActorHiddenInGame(true);

	if (IsValid(FireLight))
	{
		FireLight->SetVisibility(false);
	}
	if (IsValid(FireNiagaraComponent))
	{
		FireNiagaraComponent->DeactivateImmediate();
		FireNiagaraComponent->SetVisibility(false);
	}
	if (IsValid(SmokeNiagaraComponent))
	{
		SmokeNiagaraComponent->DeactivateImmediate();
		SmokeNiagaraComponent->SetVisibility(false);
	}
}

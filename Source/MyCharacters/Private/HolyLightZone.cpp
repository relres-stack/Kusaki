// Fill out your copyright notice in the Description page of Project Settings.

#include "HolyLightZone.h"
#include "Components/SphereComponent.h"
#include "BaseMonster.h"
#include "HolyDamageType.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"

AHolyLightZone::AHolyLightZone()
{
	PrimaryActorTick.bCanEverTick = false;

	LightZoneTrigger = CreateDefaultSubobject<USphereComponent>(TEXT("LightZoneTrigger"));
	RootComponent = LightZoneTrigger;

	LightZoneTrigger->InitSphereRadius(300.0f);
	LightZoneTrigger->SetCollisionProfileName(TEXT("OverlapAllDynamic"));
	LightZoneTrigger->SetGenerateOverlapEvents(true);
}

void AHolyLightZone::BeginPlay()
{
	Super::BeginPlay();

	GetWorldTimerManager().SetTimer(DamageTimerHandle, this, &AHolyLightZone::ApplyHolyDamageToOverlappingMonsters, DamageInterval, true);
}

void AHolyLightZone::ApplyHolyDamageToOverlappingMonsters()
{
	TArray<AActor*> OverlappingActors;
	LightZoneTrigger->GetOverlappingActors(OverlappingActors, ABaseMonster::StaticClass());

	for (AActor* Actor : OverlappingActors)
	{
		if (ABaseMonster* Monster = Cast<ABaseMonster>(Actor))
		{
			UGameplayStatics::ApplyDamage(Monster, Damage, nullptr, this, UHolyDamageType::StaticClass());
		}
	}
}

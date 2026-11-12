// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HolyLightZone.generated.h"

class USphereComponent;

// A zone of holy light that periodically deals holy damage to every ABaseMonster overlapping it.
UCLASS()
class MYCHARACTERS_API AHolyLightZone : public AActor
{
	GENERATED_BODY()

public:
	AHolyLightZone();

	// Overlap trigger; ABaseMonster actors inside this take periodic holy damage.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Holy Light")
	USphereComponent* LightZoneTrigger;

	// Holy damage dealt to each overlapping monster per damage tick.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Holy Light")
	float Damage = 15.0f;

	// Seconds between damage ticks.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Holy Light")
	float DamageInterval = 0.5f;

protected:
	virtual void BeginPlay() override;

private:
	// Applies Damage (as UHolyDamageType) to every ABaseMonster currently overlapping LightZoneTrigger.
	void ApplyHolyDamageToOverlappingMonsters();

	FTimerHandle DamageTimerHandle;
};

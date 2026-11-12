// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/DamageType.h"
#include "HolyDamageType.generated.h"

// Damage type for holy light. Monsters that only take holy damage should check
// DamageEvent.DamageTypeClass against this (or a subclass) before applying damage.
UCLASS()
class MYCHARACTERS_API UHolyDamageType : public UDamageType
{
	GENERATED_BODY()
};

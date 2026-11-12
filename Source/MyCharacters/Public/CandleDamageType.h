// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "HolyDamageType.h"
#include "CandleDamageType.generated.h"

// Holy damage dealt by the Candle. Stuns BaseMonster on hit instead of making it flee.
UCLASS()
class MYCHARACTERS_API UCandleDamageType : public UHolyDamageType
{
	GENERATED_BODY()
};

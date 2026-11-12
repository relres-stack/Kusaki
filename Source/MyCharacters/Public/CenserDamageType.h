// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "HolyDamageType.h"
#include "CenserDamageType.generated.h"

// Holy damage dealt by the Censer. Instantly kills BaseMonster on hit.
UCLASS()
class MYCHARACTERS_API UCenserDamageType : public UHolyDamageType
{
	GENERATED_BODY()
};

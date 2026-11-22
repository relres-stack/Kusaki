// Fill out your copyright notice in the Description page of Project Settings.

#include "HeroScriptNavFilter.h"

#include "NavAreas/NavArea_Obstacle.h"

UHeroScriptNavFilter::UHeroScriptNavFilter()
{
	AddTravelCostOverride(UNavArea_Obstacle::StaticClass(), 1.0f);
	AddEnteringCostOverride(UNavArea_Obstacle::StaticClass(), 0.0f);
}

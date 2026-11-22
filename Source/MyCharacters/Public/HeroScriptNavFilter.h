// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "NavFilters/NavigationQueryFilter.h"
#include "HeroScriptNavFilter.generated.h"

// Path filter for heroes walking on their own in the prologue (UHeroScriptCommand_MoveTo, task 04 step 2.5).
//
// A lit candle or censer marks the navmesh around itself as an obstacle area (AHolyItem::LightNavModifier,
// UNavArea_Obstacle, so monsters path around the light). In a hero's hand that area travels with him: a hero who
// pathfinds normally keeps routing around his own light, the path is rebuilt again and again and he circles on the
// spot (found in PIE 22.11.2026: the Priest with his starting candle could not walk anywhere). For a scripted hero
// the light area is plain floor. Monsters are not affected: they do not use this filter.
UCLASS()
class MYCHARACTERS_API UHeroScriptNavFilter : public UNavigationQueryFilter
{
	GENERATED_BODY()

public:
	UHeroScriptNavFilter();
};

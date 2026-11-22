// Fill out your copyright notice in the Description page of Project Settings.

#include "PrologueTrigger.h"

#include "BaseCharacter.h"
#include "BaseMonster.h"
#include "DrawDebugHelpers.h"
#include "EngineUtils.h"
#include "FingerFlamePeekComponent.h"
#include "Components/VisionFireSourceComponent.h"
#include "Kismet/GameplayStatics.h"
#include "PrologueDirector.h"

FString UPrologueTrigger::HeroName(FName HeroId)
{
	return HeroId.IsNone() ? FString(TEXT("player's hero")) : HeroId.ToString();
}

// ---------------------------------------------------------------------------------------------------------------
// Hero in zone

bool UPrologueTrigger_HeroInZone::Check(const APrologueDirector& Director) const
{
	const ABaseCharacter* Hero = Director.ResolveHero(HeroId);
	if (!IsValid(Hero) || ZoneTag.IsNone())
	{
		return false;
	}

	TArray<AActor*> ZoneActors;
	UGameplayStatics::GetAllActorsWithTag(&Director, ZoneTag, ZoneActors);
	const FVector HeroLocation = Hero->GetActorLocation();

	for (const AActor* Zone : ZoneActors)
	{
		if (!IsValid(Zone))
		{
			continue;
		}
		if (Radius > 0.0f)
		{
			if (FVector::Dist2D(Zone->GetActorLocation(), HeroLocation) <= Radius)
			{
				return true;
			}
		}
		else if (Zone->IsOverlappingActor(Hero) || Zone->GetComponentsBoundingBox(true).IsInsideOrOn(HeroLocation))
		{
			return true;
		}
	}
	return false;
}

void UPrologueTrigger_HeroInZone::DrawDebug(const APrologueDirector& Director) const
{
	UWorld* World = Director.GetWorld();
	if (!World || ZoneTag.IsNone())
	{
		return;
	}
	TArray<AActor*> ZoneActors;
	UGameplayStatics::GetAllActorsWithTag(&Director, ZoneTag, ZoneActors);
	for (const AActor* Zone : ZoneActors)
	{
		if (!IsValid(Zone))
		{
			continue;
		}
		if (Radius > 0.0f)
		{
			// A ring on the zone actor's height plus a tall marker, so it can be found from afar.
			DrawDebugCircle(World, Zone->GetActorLocation(), Radius, 48, FColor::Yellow, false, -1.0f, 0, 6.0f,
				FVector(1.0f, 0.0f, 0.0f), FVector(0.0f, 1.0f, 0.0f), false);
			DrawDebugLine(World, Zone->GetActorLocation(), Zone->GetActorLocation() + FVector(0.0f, 0.0f, 800.0f), FColor::Yellow, false, -1.0f, 0, 6.0f);
		}
		else
		{
			const FBox Box = Zone->GetComponentsBoundingBox(true);
			DrawDebugBox(World, Box.GetCenter(), Box.GetExtent(), FColor::Yellow, false, -1.0f, 0, 6.0f);
		}
	}
}

FString UPrologueTrigger_HeroInZone::DescribeCondition() const
{
	return Radius > 0.0f
		? FString::Printf(TEXT("%s within %.0f m of '%s'"), *HeroName(HeroId), Radius / 100.0f, *ZoneTag.ToString())
		: FString::Printf(TEXT("%s in zone '%s'"), *HeroName(HeroId), *ZoneTag.ToString());
}

// ---------------------------------------------------------------------------------------------------------------
// Hero state

bool UPrologueTrigger_HeroState::Check(const APrologueDirector& Director) const
{
	const ABaseCharacter* Hero = Director.ResolveHero(HeroId);
	if (!IsValid(Hero))
	{
		return false;
	}

	switch (State)
	{
	case EPrologueHeroState::EyesClosed:
		return Hero->IsEyesClosed();
	case EPrologueHeroState::Trapped:
		return Hero->bIsTrapped;
	case EPrologueHeroState::Mounted:
		return Hero->IsMounted();
	case EPrologueHeroState::Panicking:
		return Hero->bIsPanicking;
	case EPrologueHeroState::Crouched:
		return Hero->bIsCrouched;
	case EPrologueHeroState::FingerFlame:
	{
		const UFingerFlamePeekComponent* Flame = Hero->FindComponentByClass<UFingerFlamePeekComponent>();
		return IsValid(Flame) && Flame->bIsPeekActive;
	}
	case EPrologueHeroState::InflamedVision:
	{
		const UVisionFireSourceComponent* Vision = Hero->FindComponentByClass<UVisionFireSourceComponent>();
		return IsValid(Vision) && Vision->bIsVisionActive;
	}
	case EPrologueHeroState::Dead:
		return Hero->bIsDead;
	}
	return false;
}

FString UPrologueTrigger_HeroState::DescribeCondition() const
{
	const UEnum* Enum = StaticEnum<EPrologueHeroState>();
	const FString StateText = Enum ? Enum->GetDisplayNameTextByValue(static_cast<int64>(State)).ToString() : FString();
	return FString::Printf(TEXT("%s: %s"), *HeroName(HeroId), *StateText);
}

// ---------------------------------------------------------------------------------------------------------------
// Time elapsed

bool UPrologueTrigger_TimeElapsed::Check(const APrologueDirector& Director) const
{
	return Director.GetStepElapsedTime() >= Seconds;
}

FString UPrologueTrigger_TimeElapsed::DescribeCondition() const
{
	return FString::Printf(TEXT("%.1f s passed"), Seconds);
}

// ---------------------------------------------------------------------------------------------------------------
// No monsters near hero

bool UPrologueTrigger_NoMonstersNear::Check(const APrologueDirector& Director) const
{
	const ABaseCharacter* Hero = Director.ResolveHero(HeroId);
	UWorld* World = Director.GetWorld();
	if (!IsValid(Hero) || !World)
	{
		return false;
	}

	const FVector HeroLocation = Hero->GetActorLocation();
	const float RadiusSquared = FMath::Square(Radius);
	for (TActorIterator<ABaseMonster> It(World); It; ++It)
	{
		const ABaseMonster* Monster = *It;
		if (IsValid(Monster) && Monster->Health > 0.0f && FVector::DistSquared(Monster->GetActorLocation(), HeroLocation) < RadiusSquared)
		{
			return false;
		}
	}
	return true;
}

FString UPrologueTrigger_NoMonstersNear::DescribeCondition() const
{
	return FString::Printf(TEXT("no monsters within %.0f m of %s"), Radius / 100.0f, *HeroName(HeroId));
}

// ---------------------------------------------------------------------------------------------------------------
// All of / Any of

bool UPrologueTrigger_AllOf::Check(const APrologueDirector& Director) const
{
	for (const UPrologueTrigger* Condition : Conditions)
	{
		if (IsValid(Condition) && !Condition->IsMet(Director))
		{
			return false;
		}
	}
	return true;
}

void UPrologueTrigger_AllOf::DrawDebug(const APrologueDirector& Director) const
{
	for (const UPrologueTrigger* Condition : Conditions)
	{
		if (IsValid(Condition))
		{
			Condition->DrawDebug(Director);
		}
	}
}

void UPrologueTrigger_AnyOf::DrawDebug(const APrologueDirector& Director) const
{
	for (const UPrologueTrigger* Condition : Conditions)
	{
		if (IsValid(Condition))
		{
			Condition->DrawDebug(Director);
		}
	}
}

FString UPrologueTrigger_AllOf::DescribeCondition() const
{
	TArray<FString> Parts;
	for (const UPrologueTrigger* Condition : Conditions)
	{
		if (IsValid(Condition))
		{
			Parts.Add(Condition->Describe());
		}
	}
	return FString::Printf(TEXT("(%s)"), *FString::Join(Parts, TEXT(" AND ")));
}

bool UPrologueTrigger_AnyOf::Check(const APrologueDirector& Director) const
{
	for (const UPrologueTrigger* Condition : Conditions)
	{
		if (IsValid(Condition) && Condition->IsMet(Director))
		{
			return true;
		}
	}
	return false;
}

FString UPrologueTrigger_AnyOf::DescribeCondition() const
{
	TArray<FString> Parts;
	for (const UPrologueTrigger* Condition : Conditions)
	{
		if (IsValid(Condition))
		{
			Parts.Add(Condition->Describe());
		}
	}
	return FString::Printf(TEXT("(%s)"), *FString::Join(Parts, TEXT(" OR ")));
}

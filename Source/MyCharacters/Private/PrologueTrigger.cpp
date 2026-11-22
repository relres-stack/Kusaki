// Fill out your copyright notice in the Description page of Project Settings.

#include "PrologueTrigger.h"

#include "BaseCharacter.h"
#include "BaseMonster.h"
#include "DrawDebugHelpers.h"
#include "EngineUtils.h"
#include "FingerFlamePeekComponent.h"
#include "Components/HeroScriptRunner.h"
#include "Components/VisionFireSourceComponent.h"
#include "HeroScriptCommand.h"
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

void UPrologueTrigger_AllOf::Update(APrologueDirector& Director) const
{
	for (const UPrologueTrigger* Condition : Conditions)
	{
		if (IsValid(Condition))
		{
			Condition->Update(Director);
		}
	}
}

void UPrologueTrigger_AllOf::OnStepEnd(APrologueDirector& Director) const
{
	for (const UPrologueTrigger* Condition : Conditions)
	{
		if (IsValid(Condition))
		{
			Condition->OnStepEnd(Director);
		}
	}
}

void UPrologueTrigger_AnyOf::Update(APrologueDirector& Director) const
{
	for (const UPrologueTrigger* Condition : Conditions)
	{
		if (IsValid(Condition))
		{
			Condition->Update(Director);
		}
	}
}

void UPrologueTrigger_AnyOf::OnStepEnd(APrologueDirector& Director) const
{
	for (const UPrologueTrigger* Condition : Conditions)
	{
		if (IsValid(Condition))
		{
			Condition->OnStepEnd(Director);
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

// ---------------------------------------------------------------------------------------------------------------
// Hero does it (player or script)

void UPrologueTrigger_HeroTask::Update(APrologueDirector& Director) const
{
	if (IsValid(WhenPlayer))
	{
		WhenPlayer->Update(Director);
	}

	ABaseCharacter* Hero = Director.GetHero(HeroId);
	if (!IsValid(Hero) || Hero->IsPlayerControlled() || WhenAI.Num() == 0)
	{
		return;
	}
	// Under AI: make sure he is doing it. Started once per run of the step; if control went to the player and back,
	// the runner itself goes on with the same script.
	UHeroScriptRunner* Runner = Director.GetOrAddScriptRunner(Hero);
	const int32 Serial = Director.GetStepSerial();
	if (IsValid(Runner) && !Runner->IsRunning(this, Serial) && !Runner->HasFinished(this, Serial))
	{
		Runner->RunScript(WhenAI, false, this, Serial);
	}
}

void UPrologueTrigger_HeroTask::OnStepEnd(APrologueDirector& Director) const
{
	if (IsValid(WhenPlayer))
	{
		WhenPlayer->OnStepEnd(Director);
	}
	// The step is over (the player did it, or it was skipped): the hero must not go on with this part later.
	const ABaseCharacter* Hero = Director.GetHero(HeroId);
	UHeroScriptRunner* Runner = IsValid(Hero) ? Hero->FindComponentByClass<UHeroScriptRunner>() : nullptr;
	if (IsValid(Runner) && Runner->IsRunning(this, Director.GetStepSerial()))
	{
		Runner->StopScript();
	}
}

bool UPrologueTrigger_HeroTask::Check(const APrologueDirector& Director) const
{
	const ABaseCharacter* Hero = Director.GetHero(HeroId);
	if (!IsValid(Hero))
	{
		return false;
	}
	if (Hero->IsPlayerControlled())
	{
		return !IsValid(WhenPlayer) || WhenPlayer->IsMet(Director);
	}
	if (WhenAI.Num() == 0)
	{
		return true;
	}
	const UHeroScriptRunner* Runner = Hero->FindComponentByClass<UHeroScriptRunner>();
	return IsValid(Runner) && Runner->HasFinished(this, Director.GetStepSerial());
}

void UPrologueTrigger_HeroTask::DrawDebug(const APrologueDirector& Director) const
{
	// The player's goal (the zone) is drawn in both modes: under AI it shows where the hero is heading too.
	if (IsValid(WhenPlayer))
	{
		WhenPlayer->DrawDebug(Director);
	}
}

FString UPrologueTrigger_HeroTask::DescribeCondition() const
{
	TArray<FString> Commands;
	for (const UHeroScriptCommand* Command : WhenAI)
	{
		if (IsValid(Command))
		{
			Commands.Add(Command->Describe());
		}
	}
	return FString::Printf(TEXT("%s does it [player: %s | AI: %s]"), *HeroName(HeroId),
		IsValid(WhenPlayer) ? *WhenPlayer->Describe() : TEXT("right away"),
		Commands.Num() > 0 ? *FString::Join(Commands, TEXT(", ")) : TEXT("right away"));
}

// ---------------------------------------------------------------------------------------------------------------
// Hero finished his actions

bool UPrologueTrigger_HeroScriptDone::Check(const APrologueDirector& Director) const
{
	const ABaseCharacter* Hero = Director.ResolveHero(HeroId);
	if (!IsValid(Hero))
	{
		return false;
	}
	const UHeroScriptRunner* Runner = Hero->FindComponentByClass<UHeroScriptRunner>();
	return !IsValid(Runner) || !Runner->IsBusy();
}

FString UPrologueTrigger_HeroScriptDone::DescribeCondition() const
{
	return FString::Printf(TEXT("%s finished his actions"), *HeroName(HeroId));
}

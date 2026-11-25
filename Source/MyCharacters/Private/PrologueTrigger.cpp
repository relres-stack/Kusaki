// Fill out your copyright notice in the Description page of Project Settings.

#include "PrologueTrigger.h"

#include "BaseCharacter.h"
#include "BaseMonster.h"
#include "BinocularsPickup.h"
#include "DrawDebugHelpers.h"
#include "EngineUtils.h"
#include "FingerFlamePeekComponent.h"
#include "Components/ScriptedKnockdownComponent.h"
#include "Components/HeroScriptRunner.h"
#include "Components/VisionFireSourceComponent.h"
#include "HeroScriptCommand.h"
#include "LevelSequence.h"
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
	case EPrologueHeroState::KnockedDown:
	{
		const UScriptedKnockdownComponent* Knockdown = Hero->FindComponentByClass<UScriptedKnockdownComponent>();
		return IsValid(Knockdown) && Knockdown->IsDown();
	}
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
// Binoculars used

bool UPrologueTrigger_BinocularsUsed::Check(const APrologueDirector& Director) const
{
	for (TActorIterator<ABinocularsPickup> It(Director.GetWorld()); It; ++It)
	{
		const ABinocularsPickup* Binoculars = *It;
		if (IsValid(Binoculars) && (BinocularsTag.IsNone() || Binoculars->ActorHasTag(BinocularsTag)) && Binoculars->bHasBeenUsed)
		{
			return true;
		}
	}
	return false;
}

void UPrologueTrigger_BinocularsUsed::DrawDebug(const APrologueDirector& Director) const
{
	UWorld* World = Director.GetWorld();
	if (!World)
	{
		return;
	}
	for (TActorIterator<ABinocularsPickup> It(World); It; ++It)
	{
		const ABinocularsPickup* Binoculars = *It;
		if (IsValid(Binoculars) && (BinocularsTag.IsNone() || Binoculars->ActorHasTag(BinocularsTag)))
		{
			const FVector Location = Binoculars->GetActorLocation();
			DrawDebugLine(World, Location, Location + FVector(0.0f, 0.0f, 400.0f), FColor::Yellow, false, -1.0f, 0, 4.0f);
			DrawDebugString(World, Location + FVector(0.0f, 0.0f, 420.0f), Binoculars->bHasBeenUsed ? TEXT("binoculars (used)") : TEXT("binoculars"),
				nullptr, FColor::Yellow, 0.0f, true);
		}
	}
}

FString UPrologueTrigger_BinocularsUsed::DescribeCondition() const
{
	return BinocularsTag.IsNone() ? FString(TEXT("binoculars used"))
		: FString::Printf(TEXT("binoculars '%s' used"), *BinocularsTag.ToString());
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
// Scene finished

bool UPrologueTrigger_SceneFinished::Check(const APrologueDirector& Director) const
{
	return Director.HasSceneFinished(Scene);
}

FString UPrologueTrigger_SceneFinished::DescribeCondition() const
{
	return FString::Printf(TEXT("scene '%s' finished"), *GetNameSafe(Scene));
}

// ---------------------------------------------------------------------------------------------------------------
// Scene reached a moment

bool UPrologueTrigger_SceneTime::Check(const APrologueDirector& Director) const
{
	const float Time = Director.GetSceneTime(Scene);
	return Time >= 0.0f && Time >= Seconds;
}

FString UPrologueTrigger_SceneTime::DescribeCondition() const
{
	return FString::Printf(TEXT("scene '%s' at %.1f s"), *GetNameSafe(Scene), Seconds);
}

// ---------------------------------------------------------------------------------------------------------------
// Heroes too far apart

bool UPrologueTrigger_HeroesApart::GetAlongGoal(const APrologueDirector& Director, float& OutAhead) const
{
	const ABaseCharacter* A = Director.ResolveHero(HeroA);
	const ABaseCharacter* B = Director.ResolveHero(HeroB);
	if (GoalTag.IsNone() || !IsValid(A) || !IsValid(B))
	{
		return false;
	}
	TArray<AActor*> Goals;
	UGameplayStatics::GetAllActorsWithTag(Director.GetWorld(), GoalTag, Goals);
	const AActor* Goal = nullptr;
	float BestDistSquared = TNumericLimits<float>::Max();
	for (const AActor* Candidate : Goals)
	{
		const float DistSquared = IsValid(Candidate) ? FVector::DistSquared2D(Candidate->GetActorLocation(), B->GetActorLocation()) : BestDistSquared;
		if (DistSquared < BestDistSquared)
		{
			BestDistSquared = DistSquared;
			Goal = Candidate;
		}
	}
	if (!Goal)
	{
		return false;
	}
	const FVector Direction = (Goal->GetActorLocation() - B->GetActorLocation()).GetSafeNormal2D();
	FVector Offset = A->GetActorLocation() - B->GetActorLocation();
	Offset.Z = 0.0f;
	OutAhead = FVector::DotProduct(Offset, Direction);
	return !Direction.IsNearlyZero();
}

bool UPrologueTrigger_HeroesApart::Check(const APrologueDirector& Director) const
{
	const ABaseCharacter* A = Director.ResolveHero(HeroA);
	const ABaseCharacter* B = Director.ResolveHero(HeroB);
	if (!IsValid(A) || !IsValid(B))
	{
		return false;
	}
	if (MaxDistance > 0.0f && FVector::Dist2D(A->GetActorLocation(), B->GetActorLocation()) > MaxDistance)
	{
		return true;
	}
	float Ahead = 0.0f;
	if (GetAlongGoal(Director, Ahead))
	{
		if ((MaxAhead > 0.0f && Ahead > MaxAhead) || (MaxBehind > 0.0f && -Ahead > MaxBehind))
		{
			return true;
		}
	}
	return false;
}

void UPrologueTrigger_HeroesApart::DrawDebug(const APrologueDirector& Director) const
{
	const ABaseCharacter* A = Director.ResolveHero(HeroA);
	const ABaseCharacter* B = Director.ResolveHero(HeroB);
	if (!IsValid(A) || !IsValid(B))
	{
		return;
	}
	float Ahead = 0.0f;
	const bool bAlong = GetAlongGoal(Director, Ahead);
	const bool bMet = Check(Director);
	const FVector Lift(0.0f, 0.0f, 60.0f);
	DrawDebugLine(Director.GetWorld(), A->GetActorLocation() + Lift, B->GetActorLocation() + Lift, bMet ? FColor::Red : FColor::Green, false, -1.0f, 0, 3.0f);
	const FString Text = FString::Printf(TEXT("%.1f m%s"), FVector::Dist2D(A->GetActorLocation(), B->GetActorLocation()) / 100.0f,
		bAlong ? *FString::Printf(TEXT(", %s %.1f m"), Ahead >= 0.0f ? TEXT("ahead") : TEXT("behind"), FMath::Abs(Ahead) / 100.0f) : TEXT(""));
	DrawDebugString(Director.GetWorld(), (A->GetActorLocation() + B->GetActorLocation()) * 0.5f + Lift, Text, nullptr, bMet ? FColor::Red : FColor::Green, 0.0f, true, 1.1f);
}

FString UPrologueTrigger_HeroesApart::DescribeCondition() const
{
	TArray<FString> Parts;
	if (MaxDistance > 0.0f)
	{
		Parts.Add(FString::Printf(TEXT("farther than %.0f m"), MaxDistance / 100.0f));
	}
	if (!GoalTag.IsNone() && MaxAhead > 0.0f)
	{
		Parts.Add(FString::Printf(TEXT("more than %.0f m ahead toward '%s'"), MaxAhead / 100.0f, *GoalTag.ToString()));
	}
	if (!GoalTag.IsNone() && MaxBehind > 0.0f)
	{
		Parts.Add(FString::Printf(TEXT("more than %.0f m behind"), MaxBehind / 100.0f));
	}
	return FString::Printf(TEXT("%s and %s apart: %s"), *HeroName(HeroA), *HeroName(HeroB),
		Parts.Num() > 0 ? *FString::Join(Parts, TEXT(" or ")) : TEXT("(no limit set)"));
}

// ---------------------------------------------------------------------------------------------------------------
// Hero rides a monster of a group

bool UPrologueTrigger_HeroRidesGroupMonster::Check(const APrologueDirector& Director) const
{
	const ABaseCharacter* Hero = Director.ResolveHero(HeroId);
	if (!IsValid(Hero) || !Hero->IsMounted())
	{
		return false;
	}
	const ABaseMonster* Monster = Hero->GetMountedMonster();
	return IsValid(Monster) && (GroupName.IsNone() || Director.WasMonsterInGroup(Monster, GroupName));
}

FString UPrologueTrigger_HeroRidesGroupMonster::DescribeCondition() const
{
	return GroupName.IsNone()
		? FString::Printf(TEXT("%s rides a monster"), *HeroName(HeroId))
		: FString::Printf(TEXT("%s rides a monster of group '%s'"), *HeroName(HeroId), *GroupName.ToString());
}

// ---------------------------------------------------------------------------------------------------------------
// Monsters of a group near hero

bool UPrologueTrigger_MonsterGroupNear::Check(const APrologueDirector& Director) const
{
	const ABaseCharacter* Hero = Director.ResolveHero(HeroId);
	if (!IsValid(Hero))
	{
		return false;
	}
	int32 Near = 0;
	for (const ABaseMonster* Monster : Director.GetMonsterGroup(GroupName))
	{
		if (FVector::Dist2D(Monster->GetActorLocation(), Hero->GetActorLocation()) <= Radius)
		{
			++Near;
		}
	}
	return Near >= MinCount;
}

FString UPrologueTrigger_MonsterGroupNear::DescribeCondition() const
{
	return FString::Printf(TEXT("%d+ of group '%s' within %.1f m of %s"), MinCount, *GroupName.ToString(), Radius / 100.0f, *HeroName(HeroId));
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

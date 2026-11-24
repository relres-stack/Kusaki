// Fill out your copyright notice in the Description page of Project Settings.

#include "PrologueDirector.h"

#include "AIController.h"
#include "BaseCharacter.h"
#include "BaseMonster.h"
#include "Components/CapsuleComponent.h"
#include "Components/SphereComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HolyItem.h"
#include "Components/HeroScriptRunner.h"
#include "Components/ScriptedKnockdownComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "IntroSceneDirector.h"
#include "Kismet/GameplayStatics.h"
#include "NavigationSystem.h"
#include "PrologueAction.h"
#include "PrologueAppearance.h"
#include "PrologueScenarioData.h"
#include "PrologueTrigger.h"
#include "TimerManager.h"

APrologueDirector::APrologueDirector()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;   // ticks only once the prologue runs (debug key, readout)
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
}

void APrologueDirector::BeginPlay()
{
	Super::BeginPlay();

	if (!bRunOnBeginPlay)
	{
		UE_LOG(LogTemp, Warning, TEXT("Prologue: disabled (bRunOnBeginPlay = false)"));
		return;
	}
	if (!Scenario)
	{
		UE_LOG(LogTemp, Warning, TEXT("Prologue: no Scenario asset assigned, nothing to run"));
		ShowDebug(600, 10.0f, FColor::Red, TEXT("Prologue: no Scenario asset assigned on the director"));
		return;
	}

	for (TActorIterator<AIntroSceneDirector> It(GetWorld()); It; ++It)
	{
		Intro = *It;
		break;
	}
	TryStart();
}

void APrologueDirector::TryStart()
{
	APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
	ABaseCharacter* PlayerHero = PC ? Cast<ABaseCharacter>(PC->GetPawn()) : nullptr;

	// The opening scene owns the player's hero until it is Done (input, camera). Its own BeginPlay() may not
	// have run yet on our first try (Idle), which is why "playing" is read as "enabled and not Done yet".
	const bool bIntroRunning = IsValid(Intro) && Intro->bPlayOnBeginPlay && Intro->GetPhase() != EIntroPhase::Done;

	if (!IsValid(PlayerHero) || bIntroRunning)
	{
		// The pawn shows up within a few frames; the opening scene can take ~20 s, so only the pawn wait is capped.
		if (!IsValid(PlayerHero) && ++StartAttempts > 50)
		{
			UE_LOG(LogTemp, Warning, TEXT("Prologue: no ABaseCharacter pawn on player 0 after 10 s, prologue not started"));
			ShowDebug(600, 10.0f, FColor::Red, TEXT("Prologue: no hero pawn on player 0 - not started"));
			return;
		}
		GetWorldTimerManager().SetTimer(StartRetryHandle, this, &APrologueDirector::TryStart, 0.2f, false);
		return;
	}
	StartPrologue();
}

void APrologueDirector::StartPrologue()
{
	bStarted = true;

	RegisterExistingHeroes();
	if (bDebugSpawnMissingHeroes)
	{
		DebugSpawnMissingHeroes();
	}

	// Every hero except the one the player has goes on under its AI.
	const APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(this, 0);
	for (const TPair<FName, TObjectPtr<ABaseCharacter>>& Pair : Heroes)
	{
		if (IsValid(Pair.Value) && Pair.Value != PlayerPawn)
		{
			PutUnderAI(Pair.Value, Pair.Key);
		}
	}

	if (!Scenario->StartingHeroId.IsNone() && Scenario->StartingHeroId != GetControlledHeroId())
	{
		SwitchControlTo(Scenario->StartingHeroId);
	}

	UE_LOG(LogTemp, Warning, TEXT("Prologue: started, %d hero(es), player controls '%s', %d step(s)"),
		Heroes.Num(), *GetControlledHeroId().ToString(), Scenario->Steps.Num());
	BeginStep(0);
	SetActorTickEnabled(true);
}

void APrologueDirector::BeginStep(int32 StepIndex)
{
	CurrentStepIndex = StepIndex;
	++StepSerial;
	StepStartTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;

	if (Scenario && Scenario->Steps.IsValidIndex(StepIndex))
	{
		const FPrologueStep& Step = Scenario->Steps[StepIndex];
		UE_LOG(LogTemp, Warning, TEXT("Prologue: step %d '%s' waits for: %s"), StepIndex + 1, *Step.Name,
			IsValid(Step.Trigger) ? *Step.Trigger->Describe() : TEXT("nothing (fires right away)"));
	}
	else if (Scenario && StepIndex == Scenario->Steps.Num() && StepIndex > 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("Prologue: all %d step(s) done"), StepIndex);
	}
}

void APrologueDirector::TickSteps()
{
	if (!Scenario || !Scenario->Steps.IsValidIndex(CurrentStepIndex))
	{
		return;
	}
	const FPrologueStep& Step = Scenario->Steps[CurrentStepIndex];
	if (IsValid(Step.Trigger))
	{
		Step.Trigger->Update(*this);
	}
	if (!IsValid(Step.Trigger) || Step.Trigger->IsMet(*this))
	{
		FireCurrentStep(false);
	}
}

void APrologueDirector::FireCurrentStep(bool bForcedByDebug)
{
	if (!Scenario || !Scenario->Steps.IsValidIndex(CurrentStepIndex))
	{
		return;
	}

	// The step is read straight from the asset: actions may switch heroes or spawn actors, but never edit the
	// scenario, so the reference stays valid while they run.
	const int32 FiredIndex = CurrentStepIndex;
	const FPrologueStep& Step = Scenario->Steps[FiredIndex];

	UE_LOG(LogTemp, Warning, TEXT("Prologue: step %d '%s' fired%s after %.1f s"), FiredIndex + 1, *Step.Name,
		bForcedByDebug ? TEXT(" (debug skip)") : TEXT(""), GetStepElapsedTime());
	ShowDebug(604, 3.0f, bForcedByDebug ? FColor::Orange : FColor::Green,
		FString::Printf(TEXT("Prologue: step %d '%s' fired%s"), FiredIndex + 1, *Step.Name, bForcedByDebug ? TEXT(" (skipped)") : TEXT("")));

	// First stop what the trigger itself set going (a hero's part done by script), then the actions may give the
	// heroes new things to do.
	if (IsValid(Step.Trigger))
	{
		Step.Trigger->OnStepEnd(*this);
	}

	for (const UPrologueAction* Action : Step.Actions)
	{
		if (IsValid(Action))
		{
			UE_LOG(LogTemp, Warning, TEXT("Prologue:   - %s"), *Action->Describe());
			Action->Execute(*this);
		}
	}

	BeginStep(FiredIndex + 1);
}

UHeroScriptRunner* APrologueDirector::GetOrAddScriptRunner(ABaseCharacter* Hero)
{
	if (!IsValid(Hero))
	{
		return nullptr;
	}
	if (UHeroScriptRunner* Existing = Hero->FindComponentByClass<UHeroScriptRunner>())
	{
		return Existing;
	}

	// Added at runtime, not in ABaseCharacter's constructor (rule 1): only heroes the scenario uses get one.
	UHeroScriptRunner* Runner = NewObject<UHeroScriptRunner>(Hero, TEXT("HeroScriptRunner"));
	const FName HeroId = FindHeroId(Hero);
	const int32 HeroIndex = Scenario ? Scenario->Heroes.IndexOfByPredicate([HeroId](const FPrologueHeroEntry& Entry) { return Entry.HeroId == HeroId; }) : INDEX_NONE;
	Runner->Setup(this, HeroId, 610 + FMath::Clamp(HeroIndex, 0, 9));
	Runner->OnScriptFinished.AddDynamic(this, &APrologueDirector::HandleHeroScriptFinished);
	Runner->RegisterComponent();
	Hero->AddInstanceComponent(Runner);
	return Runner;
}

UScriptedKnockdownComponent* APrologueDirector::GetOrAddKnockdown(ABaseCharacter* Hero)
{
	if (!IsValid(Hero))
	{
		return nullptr;
	}
	if (UScriptedKnockdownComponent* Existing = Hero->FindComponentByClass<UScriptedKnockdownComponent>())
	{
		return Existing;
	}
	// Added at runtime, not in ABaseCharacter's constructor (rule 1), like the script runner.
	UScriptedKnockdownComponent* Knockdown = NewObject<UScriptedKnockdownComponent>(Hero, TEXT("ScriptedKnockdown"));
	Knockdown->RegisterComponent();
	Hero->AddInstanceComponent(Knockdown);
	return Knockdown;
}

void APrologueDirector::HandleHeroScriptFinished(UHeroScriptRunner* Runner, bool bInterrupted)
{
	if (IsValid(Runner))
	{
		ShowDebug(607, 3.0f, bInterrupted ? FColor::Orange : FColor::Green,
			FString::Printf(TEXT("Prologue: %s's script %s"), *Runner->GetHeroName(), bInterrupted ? TEXT("interrupted") : TEXT("done")));
	}
}

ABaseCharacter* APrologueDirector::ResolveHero(FName HeroId) const
{
	return HeroId.IsNone() ? Cast<ABaseCharacter>(UGameplayStatics::GetPlayerPawn(this, 0)) : GetHero(HeroId);
}

float APrologueDirector::GetStepElapsedTime() const
{
	const UWorld* World = GetWorld();
	return World ? static_cast<float>(World->GetTimeSeconds() - StepStartTime) : 0.0f;
}

void APrologueDirector::RegisterExistingHeroes()
{
	// The player's pawn first, so if two heroes in the level match the same entry, the player's one wins.
	TArray<ABaseCharacter*> Found;
	if (ABaseCharacter* PlayerHero = Cast<ABaseCharacter>(UGameplayStatics::GetPlayerPawn(this, 0)))
	{
		Found.Add(PlayerHero);
	}
	for (TActorIterator<ABaseCharacter> It(GetWorld()); It; ++It)
	{
		Found.AddUnique(*It);
	}

	for (ABaseCharacter* Hero : Found)
	{
		if (!IsValid(Hero) || Hero->bIsDead || !FindHeroId(Hero).IsNone())
		{
			continue;
		}
		for (const FPrologueHeroEntry& Entry : Scenario->Heroes)
		{
			if (Entry.HeroId.IsNone() || !Entry.HeroClass || Heroes.Contains(Entry.HeroId))
			{
				continue;
			}
			if (Hero->IsA(Entry.HeroClass))
			{
				Heroes.Add(Entry.HeroId, Hero);
				UE_LOG(LogTemp, Warning, TEXT("Prologue: hero '%s' = %s (already in the level)"), *Entry.HeroId.ToString(), *Hero->GetName());
				break;
			}
		}
	}
}

void APrologueDirector::DebugSpawnMissingHeroes()
{
	const ABaseCharacter* PlayerHero = Cast<ABaseCharacter>(UGameplayStatics::GetPlayerPawn(this, 0));
	if (!IsValid(PlayerHero))
	{
		return;
	}

	int32 Slot = 1;
	for (const FPrologueHeroEntry& Entry : Scenario->Heroes)
	{
		if (Entry.HeroId.IsNone() || !Entry.HeroClass || Heroes.Contains(Entry.HeroId))
		{
			continue;
		}
		// A row to the player's right, a step forward, facing the same way as the player's hero.
		const FVector Location = PlayerHero->GetActorLocation()
			+ PlayerHero->GetActorRightVector() * (DebugSpawnSpacing * Slot)
			+ PlayerHero->GetActorForwardVector() * 100.0f;
		SpawnHero(Entry.HeroId, FTransform(PlayerHero->GetActorRotation(), Location));
		++Slot;
	}
}

ABaseCharacter* APrologueDirector::SpawnHero(FName HeroId, const FTransform& Where)
{
	if (ABaseCharacter* Existing = GetHero(HeroId))
	{
		return Existing;
	}

	const FPrologueHeroEntry* Entry = FindEntry(HeroId);
	if (!Entry || !Entry->HeroClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("Prologue: SpawnHero('%s') - no such hero in the Scenario (or no class set)"), *HeroId.ToString());
		return nullptr;
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	ABaseCharacter* Hero = GetWorld()->SpawnActor<ABaseCharacter>(Entry->HeroClass, Where, Params);
	if (!IsValid(Hero))
	{
		UE_LOG(LogTemp, Warning, TEXT("Prologue: SpawnHero('%s') - spawn failed at %s"), *HeroId.ToString(), *Where.GetLocation().ToString());
		return nullptr;
	}

	Heroes.Add(HeroId, Hero);
	PutUnderAI(Hero, HeroId);
	UE_LOG(LogTemp, Warning, TEXT("Prologue: hero '%s' spawned at %s"), *HeroId.ToString(), *Hero->GetActorLocation().ToString());
	return Hero;
}

ABaseCharacter* APrologueDirector::AppearHero(FName HeroId)
{
	if (ABaseCharacter* Existing = GetHero(HeroId))
	{
		UE_LOG(LogTemp, Warning, TEXT("Prologue: AppearHero('%s') - already in the level, not moved"), *HeroId.ToString());
		return Existing;
	}

	const FPrologueHeroEntry* Entry = FindEntry(HeroId);
	if (!Entry || !Entry->HeroClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("Prologue: AppearHero('%s') - no such hero in the Scenario (or no class set)"), *HeroId.ToString());
		return nullptr;
	}

	FPrologueAppearancePlacement Placement;
	if (const FPrologueHeroAppearance* Rule = Scenario->Appearances.FindByPredicate(
		[HeroId](const FPrologueHeroAppearance& R) { return R.HeroId == HeroId; }))
	{
		Placement = Rule->Placement;
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("Prologue: AppearHero('%s') - no rule in Scenario->Appearances, using the default placement"), *HeroId.ToString());
	}

	// Points are on the ground; the hero's actor origin is the middle of his capsule.
	float HalfHeight = 90.0f;
	if (const ABaseCharacter* Defaults = Entry->HeroClass->GetDefaultObject<ABaseCharacter>())
	{
		if (const UCapsuleComponent* Capsule = Defaults->GetCapsuleComponent())
		{
			HalfHeight = Capsule->GetScaledCapsuleHalfHeight();
		}
	}

	FTransform Where;
	FString WhereText;
	if (!FindAppearanceTransform(Placement, HalfHeight, Where, WhereText))
	{
		UE_LOG(LogTemp, Warning, TEXT("Prologue: AppearHero('%s') - no point found (no candidate points with tag '%s', no navmesh around the player)"),
			*HeroId.ToString(), *Placement.CandidateTag.ToString());
		ShowDebug(602, 6.0f, FColor::Red, FString::Printf(TEXT("Prologue: %s could not appear - no point found"), *HeroId.ToString()));
		return nullptr;
	}

	ABaseCharacter* Hero = SpawnHero(HeroId, Where);
	if (IsValid(Hero))
	{
		UE_LOG(LogTemp, Warning, TEXT("Prologue: %s appeared - %s"), *HeroId.ToString(), *WhereText);
		ShowDebug(602, 6.0f, FColor::Green, FString::Printf(TEXT("Prologue: %s appeared - %s"), *HeroId.ToString(), *WhereText));
	}
	return Hero;
}

bool APrologueDirector::FindAppearanceTransform(const FPrologueAppearancePlacement& Placement, float CapsuleHalfHeight,
	FTransform& OutTransform, FString& OutWhere) const
{
	UWorld* World = GetWorld();
	APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
	if (!World || !IsValid(PC))
	{
		return false;
	}

	// What the player sees: his camera, not his hero's actor.
	FVector ViewLocation;
	FRotator ViewRotation;
	PC->GetPlayerViewPoint(ViewLocation, ViewRotation);
	const FVector ViewDir = ViewRotation.Vector();

	// Only static level geometry hides a point (walls, rocks, terrain): heroes and monsters walking by must not
	// change where a hero appears.
	const FCollisionQueryParams TraceParams(SCENE_QUERY_STAT(PrologueAppearance), false);
	const FCollisionObjectQueryParams StaticOnly(ECC_WorldStatic);
	auto IsVisible = [&](const FVector& Ground)
	{
		const FVector Target = Ground + FVector(0.0f, 0.0f, Placement.VisibilityTraceHeight);
		return !World->LineTraceTestByObjectType(ViewLocation, Target, StaticOnly, TraceParams);
	};

	// A point right next to another hero or a monster is taken: two heroes appearing in the same frame would
	// otherwise get the same best point and stand inside each other (their capsules stuck together).
	auto IsFree = [&](const FVector& Ground)
	{
		for (TActorIterator<APawn> It(World); It; ++It)
		{
			const FVector PawnLocation = It->GetActorLocation();
			if (FVector::Dist2D(PawnLocation, Ground) < AppearanceFreeRadius && FMath::Abs(PawnLocation.Z - Ground.Z) < 300.0f)
			{
				return false;
			}
		}
		return true;
	};
	auto IsUsable = [&](const FVector& Ground) { return IsFree(Ground) && IsVisible(Ground); };

	auto Describe = [&](const FVector& Ground)
	{
		return FString::Printf(TEXT("%.1f m, %.0f deg from the view"), FVector::Dist2D(ViewLocation, Ground) / 100.0f,
			PrologueAppearance::ViewAngleTo(ViewLocation, ViewDir, Ground));
	};

	auto DrawPoints = [&](const TArray<FVector>& Points, int32 Chosen)
	{
		if (!bShowDebug || DebugDrawPointsTime <= 0.0f)
		{
			return;
		}
		for (int32 i = 0; i < Points.Num(); ++i)
		{
			const FColor Color = (i == Chosen) ? FColor::Green
				: (PrologueAppearance::IsWithinLimits(ViewLocation, ViewDir, Points[i], Placement) ? FColor::Red : FColor(128, 128, 128));
			DrawDebugSphere(World, Points[i] + FVector(0.0f, 0.0f, 50.0f), (i == Chosen) ? 60.0f : 30.0f, 8, Color, false, DebugDrawPointsTime);
		}
		if (Points.IsValidIndex(Chosen))
		{
			DrawDebugLine(World, Points[Chosen], Points[Chosen] + FVector(0.0f, 0.0f, 600.0f), FColor::Green, false, DebugDrawPointsTime, 0, 4.0f);
		}
	};

	auto FacingViewer = [&](const FVector& Ground)
	{
		return FRotator(0.0f, (ViewLocation - Ground).Rotation().Yaw, 0.0f);
	};

	// 1. Points placed in the level in advance.
	TArray<AActor*> PointActors;
	if (!Placement.CandidateTag.IsNone())
	{
		UGameplayStatics::GetAllActorsWithTag(World, Placement.CandidateTag, PointActors);
	}
	TArray<FVector> LevelPoints;
	for (const AActor* PointActor : PointActors)
	{
		LevelPoints.Add(PointActor->GetActorLocation());
	}

	int32 Index = PrologueAppearance::ChooseBestPoint(LevelPoints, ViewLocation, ViewDir, Placement, IsUsable);
	if (Index != INDEX_NONE)
	{
		DrawPoints(LevelPoints, Index);
		const FVector Ground = LevelPoints[Index];
		OutTransform = FTransform(FRotator(0.0f, PointActors[Index]->GetActorRotation().Yaw, 0.0f), Ground + FVector(0.0f, 0.0f, CapsuleHalfHeight));
		OutWhere = FString::Printf(TEXT("point %s, %s"), *PointActors[Index]->GetActorNameOrLabel(), *Describe(Ground));
		return true;
	}

	// 2. Points around the player's view, snapped to the navmesh (the hero must be able to walk from there).
	TArray<FVector> NavPoints;
	if (UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World))
	{
		for (const FVector& Point : PrologueAppearance::MakeFallbackCandidates(ViewLocation, ViewDir, Placement))
		{
			FNavLocation Projected;
			if (Nav->ProjectPointToNavigation(Point, Projected, FVector(100.0f, 100.0f, 2000.0f)))
			{
				NavPoints.Add(Projected.Location);
			}
		}
	}

	Index = PrologueAppearance::ChooseBestPoint(NavPoints, ViewLocation, ViewDir, Placement, IsUsable);
	if (Index != INDEX_NONE)
	{
		DrawPoints(NavPoints, Index);
		const FVector Ground = NavPoints[Index];
		OutTransform = FTransform(FacingViewer(Ground), Ground + FVector(0.0f, 0.0f, CapsuleHalfHeight));
		OutWhere = FString::Printf(TEXT("around the view, %s"), *Describe(Ground));
		return true;
	}

	// 3. Nothing visible: the scenario must go on, so take the best allowed point anyway (a free one if possible).
	TArray<FVector> AllPoints = LevelPoints;
	AllPoints.Append(NavPoints);
	Index = PrologueAppearance::ChooseBestPoint(AllPoints, ViewLocation, ViewDir, Placement, IsFree);
	if (Index == INDEX_NONE)
	{
		Index = PrologueAppearance::ChooseBestPoint(AllPoints, ViewLocation, ViewDir, Placement, [](const FVector&) { return true; });
	}
	DrawPoints(AllPoints, Index);
	if (Index == INDEX_NONE)
	{
		return false;
	}
	const FVector Ground = AllPoints[Index];
	const FRotator Facing = (Index < PointActors.Num())
		? FRotator(0.0f, PointActors[Index]->GetActorRotation().Yaw, 0.0f)
		: FacingViewer(Ground);
	OutTransform = FTransform(Facing, Ground + FVector(0.0f, 0.0f, CapsuleHalfHeight));
	OutWhere = FString::Printf(TEXT("NOT VISIBLE (no visible point), %s"), *Describe(Ground));
	UE_LOG(LogTemp, Warning, TEXT("Prologue: no visible appearance point - using the best hidden one"));
	return true;
}

bool APrologueDirector::SwitchControlTo(FName HeroId)
{
	if (!bStarted)
	{
		UE_LOG(LogTemp, Warning, TEXT("Prologue: SwitchControlTo('%s') before the prologue started - ignored"), *HeroId.ToString());
		return false;
	}

	APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
	ABaseCharacter* NewHero = GetHero(HeroId);
	if (!IsValid(PC) || !IsValid(NewHero))
	{
		UE_LOG(LogTemp, Warning, TEXT("Prologue: SwitchControlTo('%s') - no such hero in the level"), *HeroId.ToString());
		return false;
	}
	if (NewHero->bIsDead)
	{
		UE_LOG(LogTemp, Warning, TEXT("Prologue: SwitchControlTo('%s') - that hero is dead"), *HeroId.ToString());
		return false;
	}

	ABaseCharacter* OldHero = Cast<ABaseCharacter>(PC->GetPawn());
	if (OldHero == NewHero)
	{
		return false;
	}
	const FName OldId = FindHeroId(OldHero);

	// Possess() lets go of the old hero (its UnPossessed() takes its HUD off the screen) and takes the new hero
	// away from its AI controller; the new hero builds its own HUD in PossessedBy().
	PC->Possess(NewHero);
	// Look the way the new hero faces, so his own camera starts straight behind him.
	PC->SetControlRotation(FRotator(0.0f, NewHero->GetActorRotation().Yaw, 0.0f));
	ReleaseFromAI(NewHero);

	if (IsValid(OldHero))
	{
		if (!OldId.IsNone())
		{
			PutUnderAI(OldHero, OldId);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("Prologue: previous pawn %s is not a prologue hero - left without a controller"), *OldHero->GetName());
		}
	}

	// Roles meant only for this stretch of control end with it (step 2.8).
	for (TPair<FName, FPrologueMonsterGroup>& Pair : MonsterGroups)
	{
		if (IsValid(Pair.Value.Role) && Pair.Value.bReleaseOnControlSwitch)
		{
			UE_LOG(LogTemp, Warning, TEXT("Prologue: group '%s' - control switched, back to their own AI"), *Pair.Key.ToString());
			SetMonsterGroupRole(Pair.Key, nullptr, 0.0f, false);
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("Prologue: control %s -> %s"), *OldId.ToString(), *HeroId.ToString());
	ShowDebug(601, 3.0f, FColor::Yellow, FString::Printf(TEXT("Prologue: control %s -> %s"), *OldId.ToString(), *HeroId.ToString()));
	OnHeroSwitched.Broadcast(HeroId);
	return true;
}

ABaseCharacter* APrologueDirector::GetHero(FName HeroId) const
{
	const TObjectPtr<ABaseCharacter>* Found = Heroes.Find(HeroId);
	return (Found && IsValid(*Found)) ? Found->Get() : nullptr;
}

FName APrologueDirector::GetControlledHeroId() const
{
	return FindHeroId(Cast<ABaseCharacter>(UGameplayStatics::GetPlayerPawn(this, 0)));
}

const FPrologueHeroEntry* APrologueDirector::FindEntry(FName HeroId) const
{
	if (!Scenario)
	{
		return nullptr;
	}
	return Scenario->Heroes.FindByPredicate([HeroId](const FPrologueHeroEntry& Entry) { return Entry.HeroId == HeroId; });
}

FName APrologueDirector::FindHeroId(const ABaseCharacter* Hero) const
{
	if (!IsValid(Hero))
	{
		return NAME_None;
	}
	for (const TPair<FName, TObjectPtr<ABaseCharacter>>& Pair : Heroes)
	{
		if (Pair.Value == Hero)
		{
			return Pair.Key;
		}
	}
	return NAME_None;
}

AAIController* APrologueDirector::GetOrCreateAIController(ABaseCharacter* Hero, FName HeroId)
{
	if (const TObjectPtr<AAIController>* Found = HeroAIControllers.Find(HeroId))
	{
		if (IsValid(*Found))
		{
			return Found->Get();
		}
	}

	// A hero placed in the level usually already has one (APawn's AutoPossessAI = PlacedInWorld) — keep it.
	if (AAIController* Current = Cast<AAIController>(Hero->GetController()))
	{
		HeroAIControllers.Add(HeroId, Current);
		return Current;
	}

	UClass* AIClass = AAIController::StaticClass();
	if (Hero->AIControllerClass && Hero->AIControllerClass->IsChildOf(AAIController::StaticClass()))
	{
		AIClass = Hero->AIControllerClass.Get();
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.ObjectFlags |= RF_Transient;
	AAIController* AICon = GetWorld()->SpawnActor<AAIController>(AIClass, Hero->GetActorLocation(), Hero->GetActorRotation(), Params);
	if (IsValid(AICon))
	{
		HeroAIControllers.Add(HeroId, AICon);
	}
	return AICon;
}

void APrologueDirector::PutUnderAI(ABaseCharacter* Hero, FName HeroId)
{
	if (!IsValid(Hero))
	{
		return;
	}

	AAIController* AICon = GetOrCreateAIController(Hero, HeroId);
	if (!IsValid(AICon))
	{
		UE_LOG(LogTemp, Warning, TEXT("Prologue: could not create an AI controller for '%s' - it will stand still"), *HeroId.ToString());
		return;
	}
	if (Hero->GetController() != AICon)
	{
		AICon->Possess(Hero);
	}
	// RMB-aiming is read straight from the key state by ABaseCharacter::Tick() only for the player's hero, but
	// bIsAiming would keep its last value; suppressing it makes sure an AI hero never stays "aiming".
	Hero->SetAimSuppressed(true);
	// Protected right away, not on the next Tick: a swing a monster started at him while he was the player's
	// hero may land this very frame.
	UpdateHeroProtection(Hero, HeroId);
}

void APrologueDirector::ReleaseFromAI(ABaseCharacter* Hero)
{
	if (IsValid(Hero))
	{
		Hero->SetAimSuppressed(false);
		UpdateHeroProtection(Hero, FindHeroId(Hero));
	}
}

void APrologueDirector::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	TickSteps();
	UpdateAllHeroProtection();
	TickMonsterRoles();
	TickDebug();
}

// ---------------------------------------------------------------------------------------------------------------
// Monster groups and roles (step 2.8)

int32 APrologueDirector::SelectMonsterGroup(FName GroupName, const FPrologueMonsterSelection& Selection)
{
	UWorld* World = GetWorld();
	if (!World || GroupName.IsNone())
	{
		UE_LOG(LogTemp, Warning, TEXT("Prologue: monster group without a name - nothing picked"));
		return 0;
	}

	const ABaseCharacter* Hero = ResolveHero(Selection.NearHeroId);
	if (!IsValid(Hero))
	{
		UE_LOG(LogTemp, Warning, TEXT("Prologue: group '%s' - hero '%s' is not in the level, counting from the director instead"),
			*GroupName.ToString(), *Selection.NearHeroId.ToString());
	}
	const FVector Center = IsValid(Hero) ? Hero->GetActorLocation() : GetActorLocation();

	TArray<TPair<float, ABaseMonster*>> Candidates;
	for (TActorIterator<ABaseMonster> It(World); It; ++It)
	{
		ABaseMonster* Monster = *It;
		if (!IsValid(Monster) || Monster->Health <= 0.0f || Monster->bIsBeingRidden)
		{
			continue;
		}
		if (!Selection.MonsterTag.IsNone() && !Monster->ActorHasTag(Selection.MonsterTag))
		{
			continue;
		}
		const FName Other = FindMonsterGroupOf(Monster);
		if (Selection.bSkipOtherGroups && !Other.IsNone() && Other != GroupName)
		{
			continue;
		}
		const float DistSquared = FVector::DistSquared2D(Monster->GetActorLocation(), Center);
		if (Selection.Radius > 0.0f && DistSquared > FMath::Square(Selection.Radius))
		{
			continue;
		}
		Candidates.Emplace(DistSquared, Monster);
	}
	Candidates.Sort([](const TPair<float, ABaseMonster*>& A, const TPair<float, ABaseMonster*>& B) { return A.Key < B.Key; });
	if (Selection.MaxCount > 0 && Candidates.Num() > Selection.MaxCount)
	{
		Candidates.SetNum(Selection.MaxCount);
	}

	TArray<TWeakObjectPtr<ABaseMonster>> Picked;
	for (const TPair<float, ABaseMonster*>& Candidate : Candidates)
	{
		Picked.Add(Candidate.Value);
	}

	// A monster picked away from another group leaves it.
	for (TPair<FName, FPrologueMonsterGroup>& Pair : MonsterGroups)
	{
		if (Pair.Key == GroupName)
		{
			continue;
		}
		for (int32 Index = Pair.Value.Members.Num() - 1; Index >= 0; --Index)
		{
			if (Picked.Contains(Pair.Value.Members[Index]))
			{
				ClearMemberState(Pair.Value.Members[Index].Get(), Pair.Value.States[Index]);
				Pair.Value.Members.RemoveAt(Index);
				Pair.Value.States.RemoveAt(Index);
			}
		}
	}

	FPrologueMonsterGroup& Group = MonsterGroups.FindOrAdd(GroupName);
	// Previous members not picked again go back to their own AI; everyone's role state starts afresh.
	for (int32 Index = 0; Index < Group.Members.Num(); ++Index)
	{
		ABaseMonster* Old = Group.Members[Index].Get();
		if (Group.States.IsValidIndex(Index))
		{
			ClearMemberState(Old, Group.States[Index]);
		}
		if (IsValid(Old) && !Picked.Contains(Group.Members[Index]))
		{
			ReleaseMonster(Old);
		}
	}
	Group.Members = Picked;
	Group.States.Reset();
	Group.States.SetNum(Picked.Num());
	if (IsValid(Group.Role))
	{
		for (const TWeakObjectPtr<ABaseMonster>& Member : Group.Members)
		{
			Member->SetScriptControlled(true);
		}
		AssignRoleSlots(Group);
	}

	UE_LOG(LogTemp, Warning, TEXT("Prologue: group '%s' - %d monster(s) picked%s%s within %.0f m of %s"), *GroupName.ToString(), Picked.Num(),
		Selection.MonsterTag.IsNone() ? TEXT("") : *FString::Printf(TEXT(" with tag '%s'"), *Selection.MonsterTag.ToString()),
		Selection.MaxCount > 0 ? *FString::Printf(TEXT(" (max %d)"), Selection.MaxCount) : TEXT(""),
		Selection.Radius / 100.0f, IsValid(Hero) ? *Hero->GetName() : TEXT("the director"));
	return Picked.Num();
}

void APrologueDirector::SetMonsterGroupRole(FName GroupName, UPrologueMonsterRole* NewRole, float Duration, bool bReleaseOnControlSwitch)
{
	FPrologueMonsterGroup* Group = MonsterGroups.Find(GroupName);
	if (!Group)
	{
		UE_LOG(LogTemp, Warning, TEXT("Prologue: no monster group '%s' (pick its monsters first) - role skipped"), *GroupName.ToString());
		return;
	}

	// The old role's setup on its monsters ends with it.
	for (int32 Index = 0; Index < Group->Members.Num() && Index < Group->States.Num(); ++Index)
	{
		ClearMemberState(Group->Members[Index].Get(), Group->States[Index]);
	}

	Group->Role = NewRole;
	Group->bReleaseOnControlSwitch = bReleaseOnControlSwitch;
	Group->EndTime = (NewRole && Duration > 0.0f && GetWorld()) ? GetWorld()->GetTimeSeconds() + Duration : 0.0;

	for (const TWeakObjectPtr<ABaseMonster>& Member : Group->Members)
	{
		if (!Member.IsValid())
		{
			continue;
		}
		if (NewRole)
		{
			Member->SetScriptControlled(true);
		}
		else
		{
			ReleaseMonster(Member.Get());
		}
	}
	if (NewRole)
	{
		AssignRoleSlots(*Group);
	}

	UE_LOG(LogTemp, Warning, TEXT("Prologue: group '%s' (%d) - %s%s"), *GroupName.ToString(), Group->Members.Num(),
		NewRole ? *NewRole->Describe() : TEXT("back to their own AI"),
		Group->EndTime > 0.0 ? *FString::Printf(TEXT(" for %.1f s"), Duration) : TEXT(""));
}

TArray<ABaseMonster*> APrologueDirector::GetMonsterGroup(FName GroupName) const
{
	TArray<ABaseMonster*> Result;
	if (const FPrologueMonsterGroup* Group = MonsterGroups.Find(GroupName))
	{
		for (const TWeakObjectPtr<ABaseMonster>& Member : Group->Members)
		{
			if (Member.IsValid() && Member->Health > 0.0f)
			{
				Result.Add(Member.Get());
			}
		}
	}
	return Result;
}

FName APrologueDirector::FindMonsterGroupOf(const ABaseMonster* Monster) const
{
	for (const TPair<FName, FPrologueMonsterGroup>& Pair : MonsterGroups)
	{
		for (const TWeakObjectPtr<ABaseMonster>& Member : Pair.Value.Members)
		{
			if (Member.Get() == Monster)
			{
				return Pair.Key;
			}
		}
	}
	return NAME_None;
}

float APrologueDirector::GetHeroLightRadius(const ABaseCharacter* Hero) const
{
	float Radius = 0.0f;
	if (!IsValid(Hero))
	{
		return Radius;
	}
	for (const AHolyItem* Item : { Hero->EquippedRightHand, Hero->EquippedLeftHand })
	{
		if (IsValid(Item) && Item->bIsLit && IsValid(Item->HolyAuraSphere))
		{
			Radius = FMath::Max(Radius, Item->HolyAuraSphere->GetScaledSphereRadius());
		}
	}
	return Radius;
}

FVector APrologueDirector::KeepOutOfHeroLight(const FVector& Goal, const ABaseMonster& Monster) const
{
	FVector Result = Goal;
	for (const TPair<FName, TObjectPtr<ABaseCharacter>>& Pair : Heroes)
	{
		const ABaseCharacter* Hero = Pair.Value;
		if (!IsValid(Hero) || Hero->bIsDead)
		{
			continue;
		}
		const FVector Center = Hero->GetActorLocation();
		for (const AHolyItem* Item : { Hero->EquippedRightHand, Hero->EquippedLeftHand })
		{
			if (!IsValid(Item) || !Item->bIsLit || !IsValid(Item->HolyAuraSphere))
			{
				continue;
			}
			// The same distance the monster's own AI keeps from a light (ABaseMonster::IsProtectedByHeldItem()), plus a
			// little so it does not stand exactly on the line.
			const float Keep = Item->HolyAuraSphere->GetScaledSphereRadius() + Monster.StalkSafetyBuffer + 30.0f;
			FVector Offset = Result - Center;
			Offset.Z = 0.0f;
			if (Offset.SizeSquared() >= FMath::Square(Keep) || !Item->IsLocationInProtectionCone(Result))
			{
				continue;
			}
			FVector Direction = Offset.GetSafeNormal();
			if (Direction.IsNearlyZero())
			{
				Direction = (Monster.GetActorLocation() - Center).GetSafeNormal2D();
			}
			if (Direction.IsNearlyZero())
			{
				Direction = Hero->GetActorForwardVector().GetSafeNormal2D();
			}
			Result = Center + Direction * Keep;
			Result.Z = Goal.Z;
		}
	}
	return Result;
}

bool APrologueDirector::WasMonsterInGroup(const ABaseMonster* Monster, FName GroupName) const
{
	if (!IsValid(Monster))
	{
		return false;
	}
	if (FindMonsterGroupOf(Monster) == GroupName)
	{
		return true;
	}
	for (const TPair<TWeakObjectPtr<ABaseMonster>, FName>& Pair : RiddenAwayFrom)
	{
		if (Pair.Key.Get() == Monster && Pair.Value == GroupName)
		{
			return true;
		}
	}
	return false;
}

void APrologueDirector::ClearMemberState(ABaseMonster* Monster, FPrologueRoleMemberState& State) const
{
	// "Sit on hero": the monster and the hero collide again.
	ABaseCharacter* Hero = State.PassThroughHero.Get();
	if (IsValid(Hero) && IsValid(Monster))
	{
		Monster->MoveIgnoreActorRemove(Hero);
		Hero->MoveIgnoreActorRemove(Monster);
	}
	State.PassThroughHero.Reset();
}

void APrologueDirector::ReleaseMonster(ABaseMonster* Monster) const
{
	if (!IsValid(Monster))
	{
		return;
	}
	Monster->SetScriptControlled(false);
	if (AAIController* AI = Cast<AAIController>(Monster->GetController()))
	{
		AI->ClearFocus(EAIFocusPriority::Gameplay);
	}
	// A role may have left it at chase speed while its own state is Patrol.
	if (UCharacterMovementComponent* Move = Monster->GetCharacterMovement())
	{
		Move->MaxWalkSpeed = (Monster->GetIsChasing() || Monster->GetIsStalking()) ? Monster->ChaseSpeed : Monster->PatrolSpeed;
	}
}

void APrologueDirector::AssignRoleSlots(FPrologueMonsterGroup& Group) const
{
	const int32 Count = Group.Members.Num();
	Group.States.SetNum(Count);
	const ABaseCharacter* Hero = IsValid(Group.Role) ? ResolveHero(Group.Role->HeroId) : nullptr;
	const FVector Center = IsValid(Hero) ? Hero->GetActorLocation() : GetActorLocation();

	// Members sorted by the angle they stand at around the hero: neighbours keep their order on the ring.
	TArray<TPair<float, int32>> ByYaw;
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const ABaseMonster* Monster = Group.Members[Index].Get();
		const FVector Offset = IsValid(Monster) ? Monster->GetActorLocation() - Center : FVector::ForwardVector;
		ByYaw.Emplace(FMath::RadiansToDegrees(FMath::Atan2(Offset.Y, Offset.X)), Index);
	}
	ByYaw.Sort([](const TPair<float, int32>& A, const TPair<float, int32>& B) { return A.Key < B.Key; });

	const float BaseYaw = ByYaw.Num() > 0 ? ByYaw[0].Key : 0.0f;
	for (int32 Slot = 0; Slot < ByYaw.Num(); ++Slot)
	{
		FPrologueRoleMemberState& State = Group.States[ByYaw[Slot].Value];
		State = FPrologueRoleMemberState();
		State.SlotIndex = Slot;
		State.SlotCount = Count;
		State.SlotBaseYaw = BaseYaw;
		State.Phase = FMath::FRandRange(0.0f, 2.0f * PI);
	}
}

void APrologueDirector::TickMonsterRoles()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	const double Now = World->GetTimeSeconds();

	for (TPair<FName, FPrologueMonsterGroup>& Pair : MonsterGroups)
	{
		FPrologueMonsterGroup& Group = Pair.Value;

		// Members fall out by themselves: dead or gone, or someone rides it (the Rider took it, step 12).
		for (int32 Index = Group.Members.Num() - 1; Index >= 0; --Index)
		{
			ABaseMonster* Monster = Group.Members[Index].Get();
			const bool bGone = !IsValid(Monster) || Monster->Health <= 0.0f;
			const bool bRidden = !bGone && Monster->bIsBeingRidden;
			if (bGone || bRidden)
			{
				ClearMemberState(Monster, Group.States[Index]);
				if (bRidden)
				{
					ReleaseMonster(Monster);
					// "Hero rides a monster of a group" still has to know where it came from.
					RiddenAwayFrom.Add(Monster, Pair.Key);
				}
				UE_LOG(LogTemp, Warning, TEXT("Prologue: group '%s' - %s leaves the group (%s)"), *Pair.Key.ToString(),
					*GetNameSafe(Monster), bRidden ? TEXT("someone rides it") : TEXT("dead"));
				Group.Members.RemoveAt(Index);
				Group.States.RemoveAt(Index);
			}
		}

		if (!IsValid(Group.Role))
		{
			continue;
		}
		if (Group.EndTime > 0.0 && Now >= Group.EndTime)
		{
			UE_LOG(LogTemp, Warning, TEXT("Prologue: group '%s' - role time is up, back to their own AI"), *Pair.Key.ToString());
			Group.Role = nullptr;
			for (int32 Index = 0; Index < Group.Members.Num(); ++Index)
			{
				ClearMemberState(Group.Members[Index].Get(), Group.States[Index]);
				ReleaseMonster(Group.Members[Index].Get());
			}
			continue;
		}

		FPrologueRoleContext Context;
		Context.Director = this;
		Context.Hero = ResolveHero(Group.Role->HeroId);
		Context.Time = Now;
		for (int32 Index = 0; Index < Group.Members.Num(); ++Index)
		{
			Context.Monster = Group.Members[Index].Get();
			Group.Role->TickMember(Context, Group.States[Index]);
		}
	}
}

void APrologueDirector::DrawMonsterGroupsDebug()
{
	if (!bShowDebug || MonsterGroups.Num() == 0)
	{
		return;
	}
	TArray<FString> Lines;
	for (const TPair<FName, FPrologueMonsterGroup>& Pair : MonsterGroups)
	{
		const FPrologueMonsterGroup& Group = Pair.Value;
		const FString RoleText = IsValid(Group.Role) ? Group.Role->Describe() : FString(TEXT("own AI"));

		// A member hiding from the sun is left alone by the role (UPrologueMonsterRole::TickMember()) — at dawn/day
		// the whole group can stand still for that reason, so it is shown, not only logged.
		const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
		int32 InSun = 0;
		int32 DrivenOff = 0;
		for (int32 Index = 0; Index < Group.Members.Num(); ++Index)
		{
			ABaseMonster* Member = Group.Members[Index].Get();
			if (!IsValid(Member))
			{
				continue;
			}
			const bool bSun = IsValid(Group.Role) && Member->GetIsFleeingToShadow();
			const bool bOff = IsValid(Group.Role) && Group.States.IsValidIndex(Index) && Group.States[Index].IsRetreating(Now);
			InSun += bSun ? 1 : 0;
			DrivenOff += bOff ? 1 : 0;
			const TCHAR* Note = !IsValid(Group.Role) ? TEXT(" (own AI)") : (bSun ? TEXT(" (sun: hides, role waits)") : (bOff ? TEXT(" (driven off)") : TEXT("")));
			DrawDebugString(GetWorld(), FVector(0.0f, 0.0f, 200.0f), Pair.Key.ToString() + Note, Member,
				!IsValid(Group.Role) ? FColor::Silver : (bSun ? FColor::Yellow : (bOff ? FColor::Green : FColor::Orange)), 0.0f, true, 1.1f);
		}
		FString Extra;
		if (DrivenOff > 0)
		{
			Extra += FString::Printf(TEXT(" - %d driven off"), DrivenOff);
		}
		if (InSun > 0)
		{
			Extra += FString::Printf(TEXT(" - %d hiding from the sun"), InSun);
		}
		Lines.Add(FString::Printf(TEXT("%s(%d): %s%s"), *Pair.Key.ToString(), Group.Members.Num(), *RoleText, *Extra));
	}
	ShowDebug(607, 0.0f, FColor::Cyan, TEXT("Monster groups: ") + FString::Join(Lines, TEXT(" | ")));
}

void APrologueDirector::SetHeroUntouchable(FName HeroId, bool bUntouchable)
{
	if (bUntouchable)
	{
		UntouchableHeroIds.Add(HeroId);
	}
	else
	{
		UntouchableHeroIds.Remove(HeroId);
	}
	UpdateHeroProtection(GetHero(HeroId), HeroId);
}

void APrologueDirector::UpdateAllHeroProtection()
{
	for (const TPair<FName, TObjectPtr<ABaseCharacter>>& Pair : Heroes)
	{
		UpdateHeroProtection(Pair.Value, Pair.Key);
	}
}

void APrologueDirector::UpdateHeroProtection(ABaseCharacter* Hero, FName HeroId)
{
	if (!IsValid(Hero) || Hero->bIsDead)
	{
		return;
	}

	// Decision 3: a hero the player does not control cannot be knocked down or killed by monsters — only by the
	// scenario. Besides, the player's hero while he lies after a protected scripted fall, and whoever a step made
	// untouchable ("Hero untouchable by monsters on/off").
	const UScriptedKnockdownComponent* Knockdown = Hero->FindComponentByClass<UScriptedKnockdownComponent>();
	const bool bUnderAI = !Hero->IsPlayerControlled();
	const bool bLying = IsValid(Knockdown) && Knockdown->IsUntouchableWhileDown();
	const bool bByStep = UntouchableHeroIds.Contains(HeroId);
	const bool bWanted = bUnderAI || bLying || bByStep;
	if (Hero->IsUntouchableByMonsters() == bWanted)
	{
		return;
	}

	Hero->SetUntouchableByMonsters(bWanted);
	const FString Why = !bWanted ? FString(TEXT("monsters can hit him again"))
		: FString::Printf(TEXT("untouchable by monsters (%s)"), bUnderAI ? TEXT("under AI") : (bLying ? TEXT("lying after a scripted fall") : TEXT("scenario step")));
	UE_LOG(LogTemp, Warning, TEXT("Prologue: %s - %s"), *HeroId.ToString(), *Why);
	ShowDebug(609, 4.0f, bWanted ? FColor::Green : FColor::Orange, FString::Printf(TEXT("Prologue: %s - %s"), *HeroId.ToString(), *Why));
}

void APrologueDirector::TickDebug()
{
	APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
	if (!IsValid(PC) || !Scenario)
	{
		return;
	}

	if (bDebugSwitchKey && DebugNextHeroKey.IsValid() && PC->WasInputKeyJustPressed(DebugNextHeroKey))
	{
		// Next living hero after the controlled one, in Scenario->Heroes order.
		TArray<FName> Order;
		for (const FPrologueHeroEntry& Entry : Scenario->Heroes)
		{
			const ABaseCharacter* Hero = GetHero(Entry.HeroId);
			if (IsValid(Hero) && !Hero->bIsDead)
			{
				Order.Add(Entry.HeroId);
			}
		}
		if (Order.Num() > 1)
		{
			const int32 Current = Order.IndexOfByKey(GetControlledHeroId());
			SwitchControlTo(Order[(Current + 1) % Order.Num()]);
		}
	}

	if (bDebugAppearKey && DebugAppearHeroKey.IsValid() && PC->WasInputKeyJustPressed(DebugAppearHeroKey))
	{
		// First hero of the list who is not in the level yet.
		const FPrologueHeroEntry* Missing = Scenario->Heroes.FindByPredicate(
			[this](const FPrologueHeroEntry& Entry) { return !Entry.HeroId.IsNone() && !IsValid(GetHero(Entry.HeroId)); });
		if (Missing)
		{
			AppearHero(Missing->HeroId);
		}
		else
		{
			ShowDebug(602, 3.0f, FColor::Yellow, TEXT("Prologue: every hero is already in the level"));
		}
	}

	if (bDebugSkipStepKey && DebugSkipStepKey.IsValid() && PC->WasInputKeyJustPressed(DebugSkipStepKey))
	{
		if (Scenario->Steps.IsValidIndex(CurrentStepIndex))
		{
			FireCurrentStep(true);
		}
		else
		{
			ShowDebug(604, 3.0f, FColor::Yellow, TEXT("Prologue: no step left to skip"));
		}
	}

	if (bShowDebug)
	{
		const FName Controlled = GetControlledHeroId();
		FString Line = FString::Printf(TEXT("Prologue | player: %s | heroes:"), *Controlled.ToString());
		for (const FPrologueHeroEntry& Entry : Scenario->Heroes)
		{
			const ABaseCharacter* Hero = GetHero(Entry.HeroId);
			const TCHAR* HeroState = !IsValid(Hero) ? TEXT("absent") : (Entry.HeroId == Controlled ? TEXT("PLAYER") : TEXT("AI"));
			// Scripted fall (step 2.6): "Priest=PLAYER(lying)".
			const UScriptedKnockdownComponent* Knockdown = IsValid(Hero) ? Hero->FindComponentByClass<UScriptedKnockdownComponent>() : nullptr;
			const FString Down = IsValid(Knockdown) && Knockdown->IsDown() ? FString::Printf(TEXT("(%s)"), *Knockdown->DescribeState()) : FString();
			// Step 2.7: "[safe]" = monsters cannot harm him right now.
			const TCHAR* Safe = IsValid(Hero) && Hero->IsUntouchableByMonsters() ? TEXT("[safe]") : TEXT("");
			Line += FString::Printf(TEXT(" %s=%s%s%s"), *Entry.HeroId.ToString(), HeroState, *Down, Safe);
		}
		if (bDebugSwitchKey)
		{
			Line += FString::Printf(TEXT(" | %s: next hero"), *DebugNextHeroKey.GetDisplayName().ToString());
		}
		if (bDebugAppearKey)
		{
			Line += FString::Printf(TEXT(" | %s: missing hero appears"), *DebugAppearHeroKey.GetDisplayName().ToString());
		}
		ShowDebug(600, 0.0f, FColor::Cyan, Line);

		// Second line: which step waits for what.
		if (Scenario->Steps.IsValidIndex(CurrentStepIndex))
		{
			const FPrologueStep& Step = Scenario->Steps[CurrentStepIndex];
			const FString Waiting = IsValid(Step.Trigger) ? Step.Trigger->Describe() : FString(TEXT("nothing"));
			if (IsValid(Step.Trigger))
			{
				Step.Trigger->DrawDebug(*this);
			}
			FString StepLine = FString::Printf(TEXT("Prologue step %d/%d '%s' (%.0f s) waits for: %s"), CurrentStepIndex + 1,
				Scenario->Steps.Num(), *Step.Name, GetStepElapsedTime(), *Waiting);
			if (bDebugSkipStepKey)
			{
				StepLine += FString::Printf(TEXT(" | %s: fire it now"), *DebugSkipStepKey.GetDisplayName().ToString());
			}
			ShowDebug(603, 0.0f, FColor::Cyan, StepLine);
		}
		else if (Scenario->Steps.Num() > 0)
		{
			ShowDebug(603, 0.0f, FColor::Cyan, FString::Printf(TEXT("Prologue: all %d step(s) done"), Scenario->Steps.Num()));
		}

		// Third line: what every hero who got a script is doing.
		TArray<FString> Scripts;
		for (const FPrologueHeroEntry& Entry : Scenario->Heroes)
		{
			const ABaseCharacter* Hero = GetHero(Entry.HeroId);
			const UHeroScriptRunner* Runner = IsValid(Hero) ? Hero->FindComponentByClass<UHeroScriptRunner>() : nullptr;
			if (IsValid(Runner))
			{
				Scripts.Add(FString::Printf(TEXT("%s: %s"), *Entry.HeroId.ToString(), *Runner->DescribeState()));
				Runner->DrawDebug();
			}
		}
		if (Scripts.Num() > 0)
		{
			ShowDebug(606, 0.0f, FColor::Cyan, TEXT("Hero scripts: ") + FString::Join(Scripts, TEXT(" | ")));
		}

		// Fourth line and labels over the monsters: monster groups and their roles (step 2.8).
		DrawMonsterGroupsDebug();
	}
}

void APrologueDirector::ShowDebug(int32 Key, float Time, const FColor& Color, const FString& Message) const
{
	if (bShowDebug && GEngine)
	{
		GEngine->AddOnScreenDebugMessage(Key, Time, Color, Message);
	}
}

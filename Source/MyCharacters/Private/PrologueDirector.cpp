// Fill out your copyright notice in the Description page of Project Settings.

#include "PrologueDirector.h"

#include "AIController.h"
#include "BaseCharacter.h"
#include "Components/CapsuleComponent.h"
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

	int32 Index = PrologueAppearance::ChooseBestPoint(LevelPoints, ViewLocation, ViewDir, Placement, IsVisible);
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

	Index = PrologueAppearance::ChooseBestPoint(NavPoints, ViewLocation, ViewDir, Placement, IsVisible);
	if (Index != INDEX_NONE)
	{
		DrawPoints(NavPoints, Index);
		const FVector Ground = NavPoints[Index];
		OutTransform = FTransform(FacingViewer(Ground), Ground + FVector(0.0f, 0.0f, CapsuleHalfHeight));
		OutWhere = FString::Printf(TEXT("around the view, %s"), *Describe(Ground));
		return true;
	}

	// 3. Nothing visible: the scenario must go on, so take the best allowed point anyway.
	TArray<FVector> AllPoints = LevelPoints;
	AllPoints.Append(NavPoints);
	Index = PrologueAppearance::ChooseBestPoint(AllPoints, ViewLocation, ViewDir, Placement, [](const FVector&) { return true; });
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
}

void APrologueDirector::ReleaseFromAI(ABaseCharacter* Hero)
{
	if (IsValid(Hero))
	{
		Hero->SetAimSuppressed(false);
	}
}

void APrologueDirector::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	TickSteps();
	TickDebug();
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
			Line += FString::Printf(TEXT(" %s=%s"), *Entry.HeroId.ToString(), HeroState);
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
	}
}

void APrologueDirector::ShowDebug(int32 Key, float Time, const FColor& Color, const FString& Message) const
{
	if (bShowDebug && GEngine)
	{
		GEngine->AddOnScreenDebugMessage(Key, Time, Color, Message);
	}
}

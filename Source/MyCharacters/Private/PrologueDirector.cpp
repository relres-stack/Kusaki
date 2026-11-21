// Fill out your copyright notice in the Description page of Project Settings.

#include "PrologueDirector.h"

#include "AIController.h"
#include "BaseCharacter.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "IntroSceneDirector.h"
#include "Kismet/GameplayStatics.h"
#include "PrologueScenarioData.h"
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

	UE_LOG(LogTemp, Warning, TEXT("Prologue: started, %d hero(es), player controls '%s'"), Heroes.Num(), *GetControlledHeroId().ToString());
	SetActorTickEnabled(true);
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
		ShowDebug(600, 0.0f, FColor::Cyan, Line);
	}
}

void APrologueDirector::ShowDebug(int32 Key, float Time, const FColor& Color, const FString& Message) const
{
	if (bShowDebug && GEngine)
	{
		GEngine->AddOnScreenDebugMessage(Key, Time, Color, Message);
	}
}

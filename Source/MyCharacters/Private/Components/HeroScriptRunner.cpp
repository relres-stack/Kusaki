// Fill out your copyright notice in the Description page of Project Settings.

#include "Components/HeroScriptRunner.h"

#include "AIController.h"
#include "BaseCharacter.h"
#include "BaseMonster.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HeroScriptCommand.h"
#include "Kismet/GameplayStatics.h"
#include "PrologueDirector.h"

UHeroScriptRunner::UHeroScriptRunner()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
}

void UHeroScriptRunner::Setup(APrologueDirector* InDirector, FName InHeroId, int32 InSubtitleKey)
{
	Director = InDirector;
	HeroId = InHeroId;
	SubtitleKey = InSubtitleKey;
}

ABaseCharacter* UHeroScriptRunner::GetHero() const
{
	return Cast<ABaseCharacter>(GetOwner());
}

AAIController* UHeroScriptRunner::GetAIController() const
{
	const ABaseCharacter* Hero = GetHero();
	return IsValid(Hero) ? Cast<AAIController>(Hero->GetController()) : nullptr;
}

FString UHeroScriptRunner::GetHeroName() const
{
	return HeroId.IsNone() ? GetNameSafe(GetOwner()) : HeroId.ToString();
}

double UHeroScriptRunner::GetWorldTime() const
{
	const UWorld* World = GetWorld();
	return World ? World->GetTimeSeconds() : 0.0;
}

void UHeroScriptRunner::RunScript(const TArray<TObjectPtr<UHeroScriptCommand>>& Commands, bool bInLoop, const UObject* InSource, int32 InSourceSerial)
{
	if (IsBusy())
	{
		UE_LOG(LogTemp, Warning, TEXT("Prologue: %s's script replaced"), *GetHeroName());
		FinishScript(true);
	}

	Script = Commands;
	Script.RemoveAll([](const TObjectPtr<UHeroScriptCommand>& Command) { return !IsValid(Command); });
	bLoop = bInLoop;
	Source = InSource;
	SourceSerial = InSourceSerial;
	bPaused = false;
	bCommandStarted = false;
	CommandIndex = 0;

	if (Script.Num() == 0)
	{
		// Empty list = "stop": nothing is left running.
		bHasScript = false;
		bFinished = false;
		CommandIndex = INDEX_NONE;
		UE_LOG(LogTemp, Warning, TEXT("Prologue: %s stops his script"), *GetHeroName());
		return;
	}

	bHasScript = true;
	bFinished = false;
	UE_LOG(LogTemp, Warning, TEXT("Prologue: %s starts a script of %d command(s)%s"), *GetHeroName(), Script.Num(), bLoop ? TEXT(", looping") : TEXT(""));
}

void UHeroScriptRunner::StopScript()
{
	if (IsBusy())
	{
		FinishScript(true);
	}
}

bool UHeroScriptRunner::IsRunning(const UObject* InSource, int32 InSourceSerial) const
{
	return IsBusy() && Source.Get() == InSource && SourceSerial == InSourceSerial;
}

bool UHeroScriptRunner::HasFinished(const UObject* InSource, int32 InSourceSerial) const
{
	return bHasScript && bFinished && Source.Get() == InSource && SourceSerial == InSourceSerial;
}

void UHeroScriptRunner::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!IsBusy())
	{
		return;
	}

	ABaseCharacter* Hero = GetHero();
	if (!IsValid(Hero) || Hero->bIsDead)
	{
		UE_LOG(LogTemp, Warning, TEXT("Prologue: %s is dead - his script ends"), *GetHeroName());
		FinishScript(true);
		return;
	}

	// Only under his own AI: the player's hero does what the player does.
	if (!IsValid(GetAIController()))
	{
		if (bCommandStarted)
		{
			EndCurrentCommand(true);
		}
		if (!bPaused)
		{
			bPaused = true;
			UE_LOG(LogTemp, Warning, TEXT("Prologue: %s's script waits - the player controls him"), *GetHeroName());
		}
		return;
	}
	if (bPaused)
	{
		bPaused = false;
		UE_LOG(LogTemp, Warning, TEXT("Prologue: %s's script goes on (command %d again from its start)"), *GetHeroName(), CommandIndex + 1);
	}

	if (!bCommandStarted)
	{
		StartCurrentCommand();
	}

	const EHeroScriptCommandStatus Status = IsValid(Current) ? Current->Tick(*this, DeltaTime) : EHeroScriptCommandStatus::Done;
	if (Status != EHeroScriptCommandStatus::Done)
	{
		return;
	}

	EndCurrentCommand(false);
	// The next command starts on the next frame: one command change per frame, so even a looping script of
	// instant commands cannot spin forever inside one frame.
	++CommandIndex;
	if (!Script.IsValidIndex(CommandIndex))
	{
		if (bLoop)
		{
			CommandIndex = 0;
		}
		else
		{
			FinishScript(false);
		}
	}
}

void UHeroScriptRunner::StartCurrentCommand()
{
	bCommandStarted = true;
	Current = nullptr;
	if (!Script.IsValidIndex(CommandIndex) || !IsValid(Script[CommandIndex]))
	{
		return;
	}

	// A fresh copy each time: commands keep their runtime state in members, the asset stays untouched.
	Current = DuplicateObject<UHeroScriptCommand>(Script[CommandIndex], this);
	UE_LOG(LogTemp, Warning, TEXT("Prologue: %s %d/%d: %s"), *GetHeroName(), CommandIndex + 1, Script.Num(), *Current->Describe());
	Current->Start(*this);
}

void UHeroScriptRunner::EndCurrentCommand(bool bInterrupted)
{
	if (IsValid(Current))
	{
		Current->End(*this, bInterrupted);
	}
	Current = nullptr;
	bCommandStarted = false;
}

void UHeroScriptRunner::FinishScript(bool bInterrupted)
{
	EndCurrentCommand(bInterrupted);
	bFinished = true;
	bPaused = false;
	UE_LOG(LogTemp, Warning, TEXT("Prologue: %s's script %s"), *GetHeroName(), bInterrupted ? TEXT("interrupted") : TEXT("done"));
	OnScriptFinished.Broadcast(this, bInterrupted);
}

FString UHeroScriptRunner::DescribeState() const
{
	if (!bHasScript)
	{
		return TEXT("idle");
	}
	if (bFinished)
	{
		return TEXT("done");
	}
	const FString What = Script.IsValidIndex(CommandIndex) && IsValid(Script[CommandIndex]) ? Script[CommandIndex]->Describe() : FString(TEXT("-"));
	const FString Progress = IsValid(Current) ? Current->DescribeProgress(*this) : FString();
	return FString::Printf(TEXT("%d/%d %s%s%s%s"), CommandIndex + 1, Script.Num(), *What,
		Progress.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" [%s]"), *Progress),
		bLoop ? TEXT(" (loop)") : TEXT(""), bPaused ? TEXT(" (waits: player controls him)") : TEXT(""));
}

void UHeroScriptRunner::DrawDebug() const
{
	if (IsValid(Current) && !bPaused)
	{
		Current->DrawDebug(*this);
	}
}

bool UHeroScriptRunner::ResolveTarget(const FHeroScriptTarget& Target, AActor*& OutActor, FVector& OutLocation) const
{
	OutActor = nullptr;
	OutLocation = FVector::ZeroVector;
	const ABaseCharacter* Hero = GetHero();
	const FVector From = IsValid(Hero) ? Hero->GetActorLocation() : FVector::ZeroVector;

	switch (Target.Type)
	{
	case EHeroScriptTargetType::ActorWithTag:
	{
		if (Target.Tag.IsNone())
		{
			return false;
		}
		TArray<AActor*> Tagged;
		UGameplayStatics::GetAllActorsWithTag(this, Target.Tag, Tagged);
		double BestDistSq = TNumericLimits<double>::Max();
		for (AActor* Actor : Tagged)
		{
			if (IsValid(Actor) && Actor != Hero)
			{
				const double DistSq = FVector::DistSquared(From, Actor->GetActorLocation());
				if (DistSq < BestDistSq)
				{
					BestDistSq = DistSq;
					OutActor = Actor;
				}
			}
		}
		break;
	}
	case EHeroScriptTargetType::Hero:
		OutActor = Director.IsValid() ? Director->ResolveHero(Target.HeroId) : nullptr;
		break;
	case EHeroScriptTargetType::NearestMonster:
	{
		double BestDistSq = TNumericLimits<double>::Max();
		for (TActorIterator<ABaseMonster> It(GetWorld()); It; ++It)
		{
			// ABaseMonster::bIsDead is private; Health is what its death is decided by.
			if (IsValid(*It) && It->Health > 0.0f)
			{
				const double DistSq = FVector::DistSquared(From, It->GetActorLocation());
				if (DistSq < BestDistSq)
				{
					BestDistSq = DistSq;
					OutActor = *It;
				}
			}
		}
		break;
	}
	case EHeroScriptTargetType::Location:
		OutLocation = Target.Location;
		return true;
	}

	if (!IsValid(OutActor))
	{
		OutActor = nullptr;
		return false;
	}
	OutLocation = OutActor->GetActorLocation();
	return true;
}

void UHeroScriptRunner::SetHeroHidden(bool bHidden)
{
	ABaseCharacter* Hero = GetHero();
	if (!IsValid(Hero) || bHidden == bHiddenByScript)
	{
		return;
	}
	bHiddenByScript = bHidden;

	UCharacterMovementComponent* MoveComp = Hero->GetCharacterMovement();
	if (bHidden)
	{
		if (AAIController* AI = GetAIController())
		{
			AI->StopMovement();
		}
		// Held candles/censers are separate actors attached to the hand: hide them too (only the ones that are
		// visible now, so showing the hero again does not reveal something hidden for another reason).
		HiddenAttachedActors.Reset();
		TArray<AActor*> Attached;
		Hero->GetAttachedActors(Attached, true, true);
		for (AActor* Actor : Attached)
		{
			if (IsValid(Actor) && !Actor->IsHidden())
			{
				Actor->SetActorHiddenInGame(true);
				HiddenAttachedActors.Add(Actor);
			}
		}
		Hero->SetActorHiddenInGame(true);
		Hero->SetActorEnableCollision(false);
		if (MoveComp)
		{
			MoveComp->DisableMovement();
		}
	}
	else
	{
		Hero->SetActorHiddenInGame(false);
		Hero->SetActorEnableCollision(true);
		if (MoveComp)
		{
			MoveComp->SetMovementMode(MOVE_Walking);
		}
		for (const TWeakObjectPtr<AActor>& Actor : HiddenAttachedActors)
		{
			if (Actor.IsValid())
			{
				Actor->SetActorHiddenInGame(false);
			}
		}
		HiddenAttachedActors.Reset();
	}
}

void UHeroScriptRunner::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (bCommandStarted)
	{
		EndCurrentCommand(true);
	}
	Super::EndPlay(EndPlayReason);
}

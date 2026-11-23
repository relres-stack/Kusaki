// Fill out your copyright notice in the Description page of Project Settings.

#include "PrologueAction.h"

#include "Animation/AnimSequence.h"
#include "BaseCharacter.h"
#include "BaseMonster.h"
#include "Components/HeroScriptRunner.h"
#include "Components/ScriptedKnockdownComponent.h"
#include "Engine/Engine.h"
#include "PrologueDirector.h"

void UPrologueAction_Message::Execute(APrologueDirector& Director) const
{
	UE_LOG(LogTemp, Warning, TEXT("Prologue message: %s"), *Text.ToString());
	if (GEngine)
	{
		// Key 605: a new line replaces the previous one instead of stacking up.
		GEngine->AddOnScreenDebugMessage(605, Duration, FColor::White, Text.ToString());
	}
}

FString UPrologueAction_Message::Describe() const
{
	return FString::Printf(TEXT("message \"%s\""), *Text.ToString());
}

void UPrologueAction_AppearHero::Execute(APrologueDirector& Director) const
{
	Director.AppearHero(HeroId);
}

FString UPrologueAction_AppearHero::Describe() const
{
	return FString::Printf(TEXT("%s appears"), *HeroId.ToString());
}

void UPrologueAction_SwitchControl::Execute(APrologueDirector& Director) const
{
	Director.SwitchControlTo(HeroId);
}

FString UPrologueAction_SwitchControl::Describe() const
{
	return FString::Printf(TEXT("control to %s"), *HeroId.ToString());
}

void UPrologueAction_HeroScript::Execute(APrologueDirector& Director) const
{
	ABaseCharacter* Hero = Director.ResolveHero(HeroId);
	UHeroScriptRunner* Runner = IsValid(Hero) ? Director.GetOrAddScriptRunner(Hero) : nullptr;
	if (!IsValid(Runner))
	{
		UE_LOG(LogTemp, Warning, TEXT("Prologue: hero '%s' is not in the level - his script is skipped"), *HeroId.ToString());
		return;
	}
	if (Hero->IsPlayerControlled() && Script.Commands.Num() > 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("Prologue: '%s' gets a script while the player controls him - it waits until he is under AI"), *Runner->GetHeroName());
	}
	Runner->RunScript(Script.Commands, Script.bLoop, this, Director.GetStepSerial());
}

FString UPrologueAction_HeroScript::Describe() const
{
	const FString Who = HeroId.IsNone() ? FString(TEXT("player's hero")) : HeroId.ToString();
	if (Script.Commands.Num() == 0)
	{
		return FString::Printf(TEXT("%s stops his script"), *Who);
	}
	return FString::Printf(TEXT("%s script: %d command(s)%s"), *Who, Script.Commands.Num(), Script.bLoop ? TEXT(", looping") : TEXT(""));
}

void UPrologueAction_KnockDown::Execute(APrologueDirector& Director) const
{
	ABaseCharacter* Hero = Director.ResolveHero(HeroId);
	UScriptedKnockdownComponent* Knockdown = IsValid(Hero) ? Director.GetOrAddKnockdown(Hero) : nullptr;
	if (!IsValid(Knockdown))
	{
		UE_LOG(LogTemp, Warning, TEXT("Prologue: hero '%s' is not in the level - knock down / get up skipped"), *HeroId.ToString());
		return;
	}
	if (bGetUp)
	{
		Knockdown->GetUp(Animation);
		return;
	}
	if (bStopHisScript)
	{
		if (UHeroScriptRunner* Runner = Hero->FindComponentByClass<UHeroScriptRunner>())
		{
			Runner->StopScript();
		}
	}
	Knockdown->KnockDown(Animation, bUntouchableWhileDown);
}

FString UPrologueAction_KnockDown::Describe() const
{
	const FString Who = HeroId.IsNone() ? FString(TEXT("player's hero")) : HeroId.ToString();
	return FString::Printf(TEXT("%s %s%s%s"), *Who, bGetUp ? TEXT("gets up") : TEXT("is knocked down"),
		Animation ? *FString::Printf(TEXT(" (%s)"), *Animation->GetName()) : TEXT(""),
		!bGetUp && !bUntouchableWhileDown ? TEXT(", monsters can hit him") : TEXT(""));
}

void UPrologueAction_HeroUntouchable::Execute(APrologueDirector& Director) const
{
	const ABaseCharacter* Hero = Director.ResolveHero(HeroId);
	const FName Id = HeroId.IsNone() ? Director.GetControlledHeroId() : HeroId;
	if (!IsValid(Hero) || Id.IsNone())
	{
		UE_LOG(LogTemp, Warning, TEXT("Prologue: hero '%s' is not in the level - untouchable on/off skipped"), *HeroId.ToString());
		return;
	}
	Director.SetHeroUntouchable(Id, bUntouchable);
}

void UPrologueAction_MonsterRole::Execute(APrologueDirector& Director) const
{
	if (bSelectMonsters)
	{
		Director.SelectMonsterGroup(GroupName, Selection);
	}
	Director.SetMonsterGroupRole(GroupName, Role, Duration, bReleaseOnControlSwitch);
}

FString UPrologueAction_MonsterRole::Describe() const
{
	return FString::Printf(TEXT("monster group '%s': %s"), *GroupName.ToString(), Role ? *Role->Describe() : TEXT("back to their own AI"));
}

void UPrologueAction_MonsterGroupDo::Execute(APrologueDirector& Director) const
{
	const ABaseCharacter* Hero = Director.ResolveHero(HeroId);
	if (!IsValid(Hero))
	{
		UE_LOG(LogTemp, Warning, TEXT("Prologue: group '%s' - hero '%s' is not in the level, '%s' skipped"),
			*GroupName.ToString(), *HeroId.ToString(), *Describe());
		return;
	}
	const FVector HeroLocation = Hero->GetActorLocation();

	// Who: members close enough to the hero, nearest first, at most MaxCount.
	TArray<ABaseMonster*> Chosen = Director.GetMonsterGroup(GroupName);
	if (Radius > 0.0f)
	{
		Chosen.RemoveAll([&](const ABaseMonster* Monster) { return FVector::Dist2D(Monster->GetActorLocation(), HeroLocation) > Radius; });
	}
	Chosen.Sort([&](const ABaseMonster& A, const ABaseMonster& B)
	{
		return FVector::DistSquared2D(A.GetActorLocation(), HeroLocation) < FVector::DistSquared2D(B.GetActorLocation(), HeroLocation);
	});
	if (MaxCount > 0 && Chosen.Num() > MaxCount)
	{
		Chosen.SetNum(MaxCount);
	}
	if (Chosen.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("Prologue: group '%s' - nobody to %s (no living member%s)"), *GroupName.ToString(), *Describe(),
			Radius > 0.0f ? *FString::Printf(TEXT(" within %.1f m of %s"), Radius / 100.0f, *Hero->GetName()) : TEXT(""));
		return;
	}

	switch (What)
	{
	case EPrologueMonsterGroupDo::KnockBack:
		for (ABaseMonster* Monster : Chosen)
		{
			FVector Away = Monster->GetActorLocation() - HeroLocation;
			Away.Z = 0.0f;
			if (!Away.Normalize())
			{
				Away = Monster->GetActorForwardVector() * -1.0f;
			}
			Monster->ApplyKnockback(Away, PushForce, UpwardForce);
		}
		break;
	case EPrologueMonsterGroupDo::Stun:
		for (ABaseMonster* Monster : Chosen)
		{
			Monster->Stun(StunDuration);
		}
		break;
	case EPrologueMonsterGroupDo::Swing:
		for (ABaseMonster* Monster : Chosen)
		{
			Monster->PlayAttackAnimation();
		}
		break;
	case EPrologueMonsterGroupDo::CallOthers:
		// One call is enough: the nearest chosen monster makes it.
		Chosen[0]->BroadcastAlert(bCallToHero ? HeroLocation : Chosen[0]->GetActorLocation());
		break;
	}
	UE_LOG(LogTemp, Warning, TEXT("Prologue: group '%s' - %s: %d monster(s)"), *GroupName.ToString(), *Describe(),
		What == EPrologueMonsterGroupDo::CallOthers ? 1 : Chosen.Num());
}

FString UPrologueAction_MonsterGroupDo::Describe() const
{
	const FString Who = HeroId.IsNone() ? FString(TEXT("player's hero")) : HeroId.ToString();
	FString Text;
	switch (What)
	{
	case EPrologueMonsterGroupDo::KnockBack:	Text = FString::Printf(TEXT("bounce off %s"), *Who); break;
	case EPrologueMonsterGroupDo::Stun:			Text = FString::Printf(TEXT("stun for %.1f s"), StunDuration); break;
	case EPrologueMonsterGroupDo::Swing:		Text = TEXT("swing (no hit)"); break;
	case EPrologueMonsterGroupDo::CallOthers:	Text = FString::Printf(TEXT("call the others to %s"), bCallToHero ? *Who : TEXT("the caller")); break;
	}
	if (MaxCount > 0)
	{
		Text += FString::Printf(TEXT(", nearest %d"), MaxCount);
	}
	if (Radius > 0.0f)
	{
		Text += FString::Printf(TEXT(", within %.1f m"), Radius / 100.0f);
	}
	return FString::Printf(TEXT("monster group '%s': %s"), *GroupName.ToString(), *Text);
}

FString UPrologueAction_HeroUntouchable::Describe() const
{
	const FString Who = HeroId.IsNone() ? FString(TEXT("player's hero")) : HeroId.ToString();
	return FString::Printf(TEXT("%s %s"), *Who, bUntouchable ? TEXT("untouchable by monsters") : TEXT("can be hit by monsters again"));
}

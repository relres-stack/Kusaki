// Fill out your copyright notice in the Description page of Project Settings.

#include "PrologueAction.h"

#include "Animation/AnimSequence.h"
#include "BaseCharacter.h"
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

FString UPrologueAction_HeroUntouchable::Describe() const
{
	const FString Who = HeroId.IsNone() ? FString(TEXT("player's hero")) : HeroId.ToString();
	return FString::Printf(TEXT("%s %s"), *Who, bUntouchable ? TEXT("untouchable by monsters") : TEXT("can be hit by monsters again"));
}

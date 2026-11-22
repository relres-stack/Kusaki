// Fill out your copyright notice in the Description page of Project Settings.

#include "PrologueAction.h"

#include "BaseCharacter.h"
#include "Components/HeroScriptRunner.h"
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

// Fill out your copyright notice in the Description page of Project Settings.

#include "DayNightManager.h"
#include "Engine/DirectionalLight.h"
#include "Engine/Engine.h"

ADayNightManager::ADayNightManager()
{
	PrimaryActorTick.bCanEverTick = true;
}

void ADayNightManager::BeginPlay()
{
	Super::BeginPlay();

	// Derive the actual per-second time multiplier from the designer-facing real-minutes duration —
	// see FullCycleDurationMinutes/DaySpeed's own comments for why this is computed here rather than
	// DaySpeed being hand-authored directly. Clamped defensively even though FullCycleDurationMinutes
	// already has ClampMin="1.0" in the Details panel — a value set via Blueprint/C++ isn't bound by that
	// UI-only clamp, and dividing by (or near) zero here would otherwise send DaySpeed to +-infinity.
	DaySpeed = 24.0f / (FMath::Max(FullCycleDurationMinutes, 0.01f) * 60.0f);

	// Seeded directly (not via UpdateSunPhase()) so the manager starts already reporting the correct
	// phase for its configured CurrentTimeOfDay without broadcasting a spurious transition event before
	// anything has had a frame to bind to it.
	SunPhase = ComputePhaseForTime(CurrentTimeOfDay);
	UpdateSunRotation();

	UE_LOG(LogTemp, Warning, TEXT("%s::BeginPlay: CurrentTimeOfDay=%.2f -> initial SunPhase=%d (full cycle %.1f real minutes, DaySpeed=%.5f)"),
		*GetName(), CurrentTimeOfDay, static_cast<int32>(SunPhase), FullCycleDurationMinutes, DaySpeed);
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(300, 3.0f, FColor::Cyan,
			FString::Printf(TEXT("DayNightManager: starting at %.2fh (phase %d), full cycle %.1f min"),
				CurrentTimeOfDay, static_cast<int32>(SunPhase), FullCycleDurationMinutes));
	}
}

void ADayNightManager::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	CurrentTimeOfDay += DaySpeed * DeltaTime;
	if (CurrentTimeOfDay >= 24.0f || CurrentTimeOfDay < 0.0f)
	{
		// Fmod alone can leave a negative result for a negative DaySpeed (C++'s fmod keeps the sign of
		// its first argument) — the extra +24/Fmod pass folds that back into the intended 0-24 range.
		CurrentTimeOfDay = FMath::Fmod(CurrentTimeOfDay, 24.0f);
		if (CurrentTimeOfDay < 0.0f)
		{
			CurrentTimeOfDay += 24.0f;
		}
	}

	UpdateSunRotation();
	UpdateSunPhase();

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(301, 0.0f, FColor::Cyan,
			FString::Printf(TEXT("Time of day: %.2fh (phase: %s)"), CurrentTimeOfDay,
				SunPhase == ESunPhase::Night ? TEXT("Night") : SunPhase == ESunPhase::Dawn ? TEXT("Dawn") : TEXT("Day")));
	}
}

void ADayNightManager::UpdateSunRotation()
{
	if (!IsValid(DirectionalLight))
	{
		return;
	}

	// See this function's header comment for why the sign here matters: a DirectionalLight shines ALONG
	// its forward vector, so noon (sun directly overhead, rays traveling straight down) needs Pitch = -90
	// (Forward.Z = sin(-90) = -1), not +90. This is "90 - sweep", not "sweep - 90".
	const float SunPitch = 90.0f - (CurrentTimeOfDay / 24.0f) * 360.0f;
	DirectionalLight->SetActorRotation(FRotator(SunPitch, 0.0f, 0.0f));
}

void ADayNightManager::UpdateSunPhase()
{
	const ESunPhase NewPhase = ComputePhaseForTime(CurrentTimeOfDay);
	if (NewPhase == SunPhase)
	{
		return;
	}

	SunPhase = NewPhase;

	UE_LOG(LogTemp, Warning, TEXT("%s: sun phase transitioned to %d at %.2fh"), *GetName(), static_cast<int32>(SunPhase), CurrentTimeOfDay);
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(302, 3.0f, FColor::Yellow,
			FString::Printf(TEXT("DayNightManager: phase -> %s at %.2fh"),
				SunPhase == ESunPhase::Night ? TEXT("Night") : SunPhase == ESunPhase::Dawn ? TEXT("Dawn") : TEXT("Day"),
				CurrentTimeOfDay));
	}

	// Each branch broadcasts exactly the ONE event matching the phase just entered — never more than one
	// per call, since NewPhase == SunPhase already returned above for a no-op tick.
	switch (SunPhase)
	{
	case ESunPhase::Dawn:
		OnDawnStarted.Broadcast();
		break;
	case ESunPhase::Day:
		OnDayStarted.Broadcast();
		break;
	case ESunPhase::Night:
		OnNightStarted.Broadcast();
		break;
	}
}

ESunPhase ADayNightManager::ComputePhaseForTime(float TimeOfDay) const
{
	// Night is the WRAPAROUND phase — it covers the stretch that crosses midnight (NightStartTime through
	// 24:00 through DawnStartTime), so it's the one expressed as an OR rather than a simple between-check.
	if (TimeOfDay >= NightStartTime || TimeOfDay < DawnStartTime)
	{
		return ESunPhase::Night;
	}
	if (TimeOfDay < DayStartTime)
	{
		return ESunPhase::Dawn;
	}
	return ESunPhase::Day;
}

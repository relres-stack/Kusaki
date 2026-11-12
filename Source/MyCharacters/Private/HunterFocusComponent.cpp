// Fill out your copyright notice in the Description page of Project Settings.

#include "HunterFocusComponent.h"
#include "BaseCharacter.h"
#include "BaseMonster.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/Engine.h"
#include "TimerManager.h"
#include "CollisionQueryParams.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

UHunterFocusComponent::UHunterFocusComponent()
{
	// Ticking is only actually needed while bIsFocusActive (to poll for Aim being released — see
	// TickComponent()) — ActivateFocus()/DeactivateFocus() flip SetComponentTickEnabled() so this stays
	// idle the rest of the time, same as any other on-demand-only component tick in this project.
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;

	// Translucent, unlit, exposes a "Color" vector parameter (base + emissive) — see MentalTrailMaterial's
	// own header comment. DefaultMarkerMaterial is captured lazily in SpawnStepMarkers() instead (from
	// MarkerMesh's own default material there), since it depends on that mesh actually loading first.
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> MentalTrailMaterialFinder(
		TEXT("/Engine/EngineDebugMaterials/M_SimpleUnlitTranslucent.M_SimpleUnlitTranslucent"));
	if (MentalTrailMaterialFinder.Succeeded())
	{
		MentalTrailMaterial = MentalTrailMaterialFinder.Object;
	}
}

void UHunterFocusComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Belt-and-braces against this component (or its owning character) being destroyed mid-Focus — same
	// "clear every timer, and undo any lasting world-state change, before this component goes away"
	// reasoning UInventoryComponent::EndPlay() already established this session. Left-running
	// GlobalTimeDilation in particular would silently leave the ENTIRE WORLD in slow-motion forever,
	// not just a dangling timer callback.
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(FocusDurationTimerHandle);
		World->GetTimerManager().ClearTimer(CooldownTimerHandle);

		if (bIsFocusActive)
		{
			UGameplayStatics::SetGlobalTimeDilation(World, 1.0f);
		}
	}

	ClearStepMarkers();

	Super::EndPlay(EndPlayReason);
}

void UHunterFocusComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// Only ever runs while bIsFocusActive (see SetComponentTickEnabled() in ActivateFocus()/
	// DeactivateFocus()) — releasing Aim mid-Focus interrupts it exactly like letting it time out or
	// pressing F again, via the same DeactivateFocus() cleanup path.
	const ABaseCharacter* OwnerCharacter = Cast<ABaseCharacter>(GetOwner());
	if (!IsValid(OwnerCharacter) || !OwnerCharacter->GetIsAiming())
	{
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(244, 2.0f, FColor::Orange, TEXT("Hunter's Focus: interrupted (Aim released)"));
		}
		DeactivateFocus();
	}
}

void UHunterFocusComponent::ToggleFocus()
{
	if (bIsFocusActive)
	{
		DeactivateFocus();
		return;
	}

	if (!bCanUseFocus)
	{
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(1005, 2.0f, FColor::Red, TEXT("Hunter's Focus: not ready yet"));
		}
		return;
	}

	// Aimed ability only — F alone (without RMB held) must not start the count. GetIsAiming() is
	// ABaseCharacter's own every-Tick-authoritative flag (see its Tick() comment on IsInputKeyDown()), so
	// this can't desync from what RMB is actually doing.
	const ABaseCharacter* OwnerCharacter = Cast<ABaseCharacter>(GetOwner());
	if (!IsValid(OwnerCharacter) || !OwnerCharacter->GetIsAiming())
	{
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(243, 2.0f, FColor::Orange, TEXT("Hunter's Focus: hold Aim (RMB) to use"));
		}
		return;
	}

	ActivateFocus();
}

void UHunterFocusComponent::ActivateFocus()
{
	ABaseCharacter* OwnerCharacter = Cast<ABaseCharacter>(GetOwner());
	UWorld* World = GetWorld();
	if (!IsValid(OwnerCharacter) || !World)
	{
		return;
	}

	bIsFocusActive = true;
	bCanUseFocus = false;

	// Enabled only while active — see TickComponent()'s own comment.
	SetComponentTickEnabled(true);

	// A fresh snapshot clears whatever's left from the previous activation (normally nothing — see
	// DeactivateFocus()'s own comment on why it no longer destroys markers itself) before spawning new
	// ones, so re-activating Focus can never accumulate/duplicate markers from an earlier still-lingering
	// Mental Trail.
	ClearStepMarkers();

	UGameplayStatics::SetGlobalTimeDilation(World, TimeDilationFactor);

	// Trace + step count + markers: a one-shot snapshot taken right now, not re-evaluated for the rest of
	// the active window — see this class's own header comment on why.
	FVector TargetPoint;
	FHitResult Hit;
	const bool bHitSomething = OwnerCharacter->TraceHunterFocusTarget(FocusTraceDistance, TargetPoint, Hit);

	// Distance measured from the CHARACTER to TargetPoint (2D, matching Direction below) — NOT
	// Hit.Distance, which is measured from the CAMERA (TraceHunterFocusTarget() traces from the camera's
	// own location — see its comment). Using Hit.Distance previously overcounted every step by however
	// far back the spring arm sits behind the character (~400 units by default, i.e. ~4 extra "steps" at
	// the default 100-unit StepDistance), since SpawnStepMarkers() below walks the markers out from the
	// CHARACTER's location, not the camera's — this now matches what it actually spawns/what the player
	// actually walks.
	const float Distance = FVector::Dist2D(OwnerCharacter->GetActorLocation(), TargetPoint);
	StepsToTarget = FMath::Max(1, FMath::RoundToInt(Distance / FMath::Max(1.0f, StepDistance)));
	bTargetIsMonster = bHitSomething && IsValid(Cast<ABaseMonster>(Hit.GetActor()));

	const FVector Direction = (TargetPoint - OwnerCharacter->GetActorLocation()).GetSafeNormal2D();
	SpawnStepMarkers(OwnerCharacter->GetActorLocation(), Direction);

	UE_LOG(LogTemp, Warning, TEXT("%s: Hunter's Focus activated — %d steps to %s"), *GetName(), StepsToTarget,
		bTargetIsMonster ? TEXT("a monster") : TEXT("target point"));
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(1006, FocusDuration, FColor::Cyan,
			FString::Printf(TEXT("Hunter's Focus: %d steps%s"), StepsToTarget, bTargetIsMonster ? TEXT(" (MONSTER)") : TEXT("")));
	}

	World->GetTimerManager().SetTimer(FocusDurationTimerHandle, this, &UHunterFocusComponent::DeactivateFocus, FocusDuration, false);
}

void UHunterFocusComponent::DeactivateFocus()
{
	if (!bIsFocusActive)
	{
		return;
	}
	bIsFocusActive = false;

	SetComponentTickEnabled(false);

	UWorld* World = GetWorld();
	if (World)
	{
		// Clears cleanly whether this run because the timer actually fired (harmless no-op on an
		// already-expired one-shot timer) or because ToggleFocus() called this early.
		World->GetTimerManager().ClearTimer(FocusDurationTimerHandle);
		UGameplayStatics::SetGlobalTimeDilation(World, 1.0f);
	}

	// Deliberately does NOT clear step markers here anymore (regression found by manual test,
	// 14.08.2026-2): this fires the instant Aim is released (see TickComponent()) or the FocusDuration
	// timer expires, EITHER of which can happen well before the player has had a chance to close their
	// eyes and look at the Mental Trail — destroying the markers here made the whole Mental Trail feature
	// (closing eyes to see the afterimage of a Focus that already ended) impossible to ever actually use.
	// Markers now persist until: ActivateFocus()'s own snapshot-clear on the NEXT activation, eyes
	// reopening after Focus already ended (see SetMentalTrailActive()), or EndPlay()'s belt-and-braces
	// cleanup.

	if (World)
	{
		World->GetTimerManager().SetTimer(CooldownTimerHandle, this, &UHunterFocusComponent::EndCooldown, FocusCooldown, false);
	}
}

void UHunterFocusComponent::EndCooldown()
{
	bCanUseFocus = true;

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(242, 2.0f, FColor::Green, TEXT("Hunter's Focus ready"));
	}
}

void UHunterFocusComponent::SpawnStepMarkers(const FVector& Origin, const FVector& Direction)
{
	UWorld* World = GetWorld();
	ABaseCharacter* OwnerCharacter = Cast<ABaseCharacter>(GetOwner());
	if (!World || !IsValid(OwnerCharacter) || Direction.IsNearlyZero())
	{
		return;
	}

	UStaticMesh* MarkerMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (!DefaultMarkerMaterial && MarkerMesh)
	{
		// Captured once, lazily — the FIRST marker spawn's mesh determines it for every marker afterward
		// (they all share MarkerMesh, so its default material never actually varies call to call).
		DefaultMarkerMaterial = MarkerMesh->GetMaterial(0);
	}

	// Freshly-spawned markers should already reflect the character's CURRENT eyes-open/closed state
	// (e.g. Focus activated while eyes were already closed from an earlier CloseEyes()) rather than
	// defaulting to "normal" until the next OnEyesClosedChanged() call.
	bMentalTrailActive = OwnerCharacter->IsEyesClosed();

	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(OwnerCharacter);

	for (int32 StepIndex = 1; StepIndex <= StepsToTarget; ++StepIndex)
	{
		const FVector StepPoint = Origin + Direction * (StepIndex * StepDistance);

		// Short downward trace to place the marker ON the actual ground at this step, not assuming flat
		// terrain — same "trace down from a computed XY point" approach AHolyItem's boundary-circle
		// ground-snapping already uses.
		const FVector TraceStart = StepPoint + FVector(0.0f, 0.0f, 200.0f);
		const FVector TraceEnd = StepPoint - FVector(0.0f, 0.0f, 500.0f);
		FHitResult GroundHit;
		const FVector MarkerLocation = World->LineTraceSingleByChannel(GroundHit, TraceStart, TraceEnd, ECC_Visibility, QueryParams)
			? GroundHit.ImpactPoint
			: FVector(StepPoint.X, StepPoint.Y, Origin.Z);

		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AStaticMeshActor* Marker = World->SpawnActor<AStaticMeshActor>(MarkerLocation, FRotator::ZeroRotator, SpawnParams);
		if (IsValid(Marker) && IsValid(Marker->GetStaticMeshComponent()))
		{
			UStaticMeshComponent* MeshComp = Marker->GetStaticMeshComponent();
			MeshComp->SetMobility(EComponentMobility::Movable);
			MeshComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			if (MarkerMesh)
			{
				MeshComp->SetStaticMesh(MarkerMesh);
			}
			// Flat disc, not a full cylinder — reads as a "footstep marker" lying on the ground rather
			// than a post sticking up out of it.
			Marker->SetActorScale3D(FVector(0.2f, 0.2f, 0.02f));

			if (bMentalTrailActive && IsValid(MentalTrailMaterial))
			{
				if (UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(MentalTrailMaterial, Marker))
				{
					MID->SetVectorParameterValue(TEXT("Color"), MentalTrailColor);
					MeshComp->SetMaterial(0, MID);
				}
			}

			StepMarkers.Add(Marker);
		}
	}
}

void UHunterFocusComponent::SetMentalTrailActive(bool bActive)
{
	bMentalTrailActive = bActive;

	for (AActor* Marker : StepMarkers)
	{
		AStaticMeshActor* MarkerActor = Cast<AStaticMeshActor>(Marker);
		if (!IsValid(MarkerActor) || !IsValid(MarkerActor->GetStaticMeshComponent()))
		{
			continue;
		}

		UStaticMeshComponent* MeshComp = MarkerActor->GetStaticMeshComponent();
		if (bActive && IsValid(MentalTrailMaterial))
		{
			if (UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(MentalTrailMaterial, MarkerActor))
			{
				MID->SetVectorParameterValue(TEXT("Color"), MentalTrailColor);
				MeshComp->SetMaterial(0, MID);
			}
		}
		else if (IsValid(DefaultMarkerMaterial))
		{
			MeshComp->SetMaterial(0, DefaultMarkerMaterial);
		}
	}

	// Eyes reopening while Focus has ALREADY ended means the player just finished looking at the Mental
	// Trail — clear the leftover markers now instead of leaving them in the world indefinitely. While
	// Focus is still active, do nothing extra here: ActivateFocus()'s snapshot-clear/DeactivateFocus()
	// governs marker lifetime instead, same as if eyes had never been closed at all.
	if (!bActive && !bIsFocusActive)
	{
		ClearStepMarkers();
	}
}

void UHunterFocusComponent::ClearStepMarkers()
{
	for (AActor* Marker : StepMarkers)
	{
		if (IsValid(Marker))
		{
			Marker->Destroy();
		}
	}
	StepMarkers.Reset();
}

// Fill out your copyright notice in the Description page of Project Settings.

#include "RedneckCharacter.h"
#include "MoonshineComponent.h"
#include "HunterFocusComponent.h"
#include "FlashlightComponent.h"
#include "InventoryComponent.h"
#include "Components/RageFallComponent.h"
#include "Components/MoonshineScentComponent.h"
#include "BinocularsPickup.h"
#include "DrunkNPC.h"
#include "Kismet/GameplayStatics.h"
#include "Components/SpotLightComponent.h"
#include "Components/InputComponent.h"
#include "InputCoreTypes.h"

ARedneckCharacter::ARedneckCharacter()
{
	MoonshineComponent = CreateDefaultSubobject<UMoonshineComponent>(TEXT("MoonshineComponent"));
	HunterFocusComponent = CreateDefaultSubobject<UHunterFocusComponent>(TEXT("HunterFocusComponent"));
	RageFallComponent = CreateDefaultSubobject<URageFallComponent>(TEXT("RageFallComponent"));
	MoonshineScentComponent = CreateDefaultSubobject<UMoonshineScentComponent>(TEXT("MoonshineScentComponent"));

	FlashlightLight = CreateDefaultSubobject<USpotLightComponent>(TEXT("FlashlightLight"));
	FlashlightLight->SetupAttachment(GetMesh(), FlashlightSocket);
	FlashlightLight->Intensity = 8000.0f;
	FlashlightLight->OuterConeAngle = 30.0f;
	FlashlightLight->AttenuationRadius = 2000.0f;
	FlashlightLight->SetVisibility(true);

	FlashlightComponent = CreateDefaultSubobject<UFlashlightComponent>(TEXT("FlashlightComponent"));
	FlashlightComponent->LightComponent = FlashlightLight;
}

void ARedneckCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	// V is deliberately bound to nothing here anymore — Bulls Charge (the old "Drink"/V action) now fires
	// through TryHijackAttackPress() on the shared RMB+LMB "Throw"/Attack binding in
	// ABaseCharacter::SetupPlayerInputComponent() instead. V is free for future use.

	// "Flashlight" action mapping bound to T. BindKey() directly too.
	PlayerInputComponent->BindAction("Flashlight", IE_Pressed, this, &ARedneckCharacter::OnFlashlightPressed);
	PlayerInputComponent->BindKey(EKeys::T, IE_Pressed, this, &ARedneckCharacter::OnFlashlightPressed);

	// Hunter's Focus — no named Input Action mapping exists for this (same reasoning as Bear Trap's
	// MashTrapEscape() reusing E directly in ABaseCharacter::SetupPlayerInputComponent()), so this is a
	// direct BindKey() only. F is otherwise only used by ARiderCharacter's own Mount binding — a
	// completely different character class, so no actual conflict. Reserved exclusively for this
	// mechanic — see OnHunterFocusPressed()'s own header comment.
	PlayerInputComponent->BindKey(EKeys::F, IE_Pressed, this, &ARedneckCharacter::OnHunterFocusPressed);

	// "UseMoonshine" action mapping bound to Q in DefaultInput.ini. BindKey() directly too, same fallback
	// pattern this project's other actions already use. Q is also "MeleeSwing" in
	// ABaseCharacter::SetupPlayerInputComponent() — both fire on the same press, same "shared key, each
	// self-gates" pattern as E/Space elsewhere in this class hierarchy; see OnUseMoonshinePressed()'s own
	// header comment.
	PlayerInputComponent->BindAction("UseMoonshine", IE_Pressed, this, &ARedneckCharacter::OnUseMoonshinePressed);
	PlayerInputComponent->BindKey(EKeys::Q, IE_Pressed, this, &ARedneckCharacter::OnUseMoonshinePressed);
}

bool ARedneckCharacter::TryHijackAttackPress()
{
	// Decoy throw — checked FIRST, ahead of Bulls Charge below: while aiming with a ADrunkNPC carried
	// (see Interact()'s own feed-the-decoy branch), LMB throws it at the same aim-target point
	// TraceAimTarget() already computes for every other throw/mount trace in this project, instead of
	// either the normal ThrowItem() throw or a Bulls Charge dash. Consumed unconditionally once reached
	// (CarriedNPC cleared, returns true) — same "never fall through once hijacked" contract
	// TryHijackAttackPress() already has for Bulls Charge below.
	if (bIsAiming && IsValid(CarriedNPC))
	{
		FVector TargetPoint;
		FHitResult AimHit;
		TraceAimTarget(TargetPoint, AimHit);

		CarriedNPC->ThrowTo(TargetPoint);
		CarriedNPC = nullptr;
		return true;
	}

	// RMB (Aim) + LMB (Attack) is the Bulls Charge combo, but it only ever READS whether the Moonshine
	// buff is already active — it never activates or spends it (that's UseMoonshine(), called separately
	// from the inventory consumable slot). Without RMB held, or with the buff not active, this returns
	// false and ThrowItem() falls through to its own normal aimed throw/attack instead — no dash without
	// the buff already running.
	if (!bIsAiming || !IsValid(MoonshineComponent) || !MoonshineComponent->bIsMoonshineActive)
	{
		return false;
	}

	// Buff active: a deliberate directional lunge, not just the buff's own passive "light bump while
	// moving" ram (UMoonshineComponent::OnOwnerCapsuleHit()) — see PerformRageDash()'s own comment.
	// Consumed unconditionally once reached: LMB while aiming with the buff active is dedicated entirely
	// to the dash for this hero; it must never fall through to ThrowItem()'s normal throw.
	MoonshineComponent->PerformRageDash();

	if (IsValid(RageFallComponent))
	{
		RageFallComponent->StartDashResolutionWindow();
	}

	// MoonshineScentComponent is NOT triggered here — its scent trail starts on its own, from the buff's
	// end (bIsMoonshineActive true -> false), not from the dash itself. See its own header comment.

	return true;
}

void ARedneckCharacter::Interact()
{
	// Checked BEFORE ever touching the binoculars — while trapped, E must only ever drive
	// MashTrapEscape()/Super::Interact()'s own bIsTrapped guard, even if a Bear Trap happens to catch
	// someone near the binoculars.
	if (!bIsTrapped)
	{
		TArray<AActor*> FoundBinoculars;
		UGameplayStatics::GetAllActorsOfClass(GetWorld(), ABinocularsPickup::StaticClass(), FoundBinoculars);

		for (AActor* Actor : FoundBinoculars)
		{
			ABinocularsPickup* Pickup = Cast<ABinocularsPickup>(Actor);
			if (!IsValid(Pickup) || Pickup->bHasBeenUsed)
			{
				continue;
			}

			if (FVector::Dist(GetActorLocation(), Pickup->GetActorLocation()) <= Pickup->InteractRadius)
			{
				Pickup->LookThroughBinoculars();
				return;
			}
		}

		// Feed a decoy Moonshine — only if not already carrying one (CarriedNPC == nullptr). A simple
		// fixed-radius proximity check (same "SphereOverlapActors, no dedicated scan" reasoning as
		// ABearTrap/AHookPoint's own proximity logic), not the interact-scan trace/HighlightedItem system
		// (which only ever tracks AHolyItem — see ABaseCharacter::ScanForInteractable()'s own comment),
		// so this never competes with that system.
		if (!IsValid(CarriedNPC))
		{
			constexpr float DrunkNPCInteractRadius = 200.0f;
			TArray<AActor*> FoundNPCs;
			UGameplayStatics::GetAllActorsOfClass(GetWorld(), ADrunkNPC::StaticClass(), FoundNPCs);

			for (AActor* Actor : FoundNPCs)
			{
				ADrunkNPC* Found = Cast<ADrunkNPC>(Actor);
				if (!IsValid(Found) || Found->State != EDrunkNPCState::Idle)
				{
					continue;
				}

				if (FVector::Dist(GetActorLocation(), Found->GetActorLocation()) <= DrunkNPCInteractRadius
					&& Found->TryGetDrunk(MoonshineComponent))
				{
					CarriedNPC = Found;
					return;
				}
			}
		}
	}

	Super::Interact();
}

bool ARedneckCharacter::IsImmuneToMonsterCatch() const
{
	return IsValid(MoonshineComponent) && MoonshineComponent->bIsMoonshineActive;
}

FString ARedneckCharacter::GetHunterFocusPromptText() const
{
	if (!IsValid(HunterFocusComponent) || !HunterFocusComponent->bIsFocusActive)
	{
		return FString();
	}

	return HunterFocusComponent->bTargetIsMonster
		? FString::Printf(TEXT("%d STEPS TO TARGET"), HunterFocusComponent->StepsToTarget)
		: FString::Printf(TEXT("SAFE STEPS: %d"), HunterFocusComponent->StepsToTarget);
}

void ARedneckCharacter::OnHunterFocusPressed()
{
	if (IsValid(HunterFocusComponent))
	{
		HunterFocusComponent->ToggleFocus();
	}
}

void ARedneckCharacter::OnEyesClosedChanged(bool bClosed)
{
	if (IsValid(HunterFocusComponent))
	{
		HunterFocusComponent->SetMentalTrailActive(bClosed);
	}
}

void ARedneckCharacter::OnUseMoonshinePressed()
{
	// Routed through the Consumable quick slot (UInventoryComponent::UseConsumableSlot()) rather than
	// calling MoonshineComponent->UseMoonshine() directly — drinking is the Moonshine ITEM's use action,
	// not a standalone ability divorced from the inventory it's shown in, and UseConsumableSlot() also
	// refreshes the HUD's consumable fill bar immediately on success. This is Tier 1 ONLY (see
	// UMoonshineComponent's own header comment) — it never dashes; that's
	// TryHijackAttackPress()/PerformRageDash() via the separate RMB+LMB combo.
	if (IsValid(InventoryComponent))
	{
		InventoryComponent->UseConsumableSlot();
	}
}

void ARedneckCharacter::OnFlashlightPressed()
{
	if (IsValid(FlashlightComponent))
	{
		FlashlightComponent->Toggle();

		// Immediate HUD refresh — RefreshLightSourceSlot() falls back to the flashlight only when neither
		// hand holds a lit item (see its own comment), so toggling it on/off directly changes what the
		// Light Source quick-slot shows; same "don't wait for the next auto-refresh tick" reasoning as
		// ABaseCharacter::PickUp()/Interact()'s own matching calls.
		if (IsValid(InventoryComponent))
		{
			InventoryComponent->RefreshLightSourceSlot();
		}
	}
}

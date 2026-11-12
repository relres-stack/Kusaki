// Fill out your copyright notice in the Description page of Project Settings.

#include "InventoryComponent.h"
#include "BaseCharacter.h"
#include "RedneckCharacter.h"
#include "SiblingCharacter.h"
#include "MoonshineComponent.h"
#include "FlashlightComponent.h"
#include "HolyItem.h"
#include "TimerManager.h"

namespace
{
	// Slot roles are fixed by index — see EQuickSlotType's own comment.
	constexpr int32 LightSourceSlotIndex = 0;
	constexpr int32 ConsumableSlotIndex = 1;
}

UInventoryComponent::UInventoryComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	QuickSlots.SetNum(NumQuickSlots);
}

void UInventoryComponent::BeginPlay()
{
	Super::BeginPlay();

	OwnerCharacter = Cast<ABaseCharacter>(GetOwner());

	RefreshLightSourceSlot();
	RefreshConsumableSlot();

	GetWorld()->GetTimerManager().SetTimer(AutoRefreshTimerHandle, FTimerDelegate::CreateLambda([this]()
	{
		RefreshLightSourceSlot();
		RefreshConsumableSlot();
	}), AutoRefreshInterval, true);
}

void UInventoryComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Must happen before Super::EndPlay() (and unconditionally, regardless of EndPlayReason) — see this
	// function's own header comment: AutoRefreshTimerHandle's lambda captures a raw `this`, so leaving it
	// running past this component's destruction is a use-after-free waiting to happen the next time it
	// fires, not just a stale-data bug.
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(AutoRefreshTimerHandle);
	}

	Super::EndPlay(EndPlayReason);
}

FQuickSlotData UInventoryComponent::GetSlotData(int32 SlotIndex) const
{
	if (!QuickSlots.IsValidIndex(SlotIndex))
	{
		return FQuickSlotData();
	}
	return QuickSlots[SlotIndex];
}

void UInventoryComponent::SelectSlot(int32 SlotIndex)
{
	if (!QuickSlots.IsValidIndex(SlotIndex) || SlotIndex == ActiveSlotIndex)
	{
		return;
	}

	const int32 OldIndex = ActiveSlotIndex;
	ActiveSlotIndex = SlotIndex;
	OnActiveSlotChanged.Broadcast(ActiveSlotIndex, OldIndex);
}

void UInventoryComponent::SelectNextSlot()
{
	SelectSlot((ActiveSlotIndex + 1) % NumQuickSlots);
}

void UInventoryComponent::SelectPreviousSlot()
{
	SelectSlot((ActiveSlotIndex - 1 + NumQuickSlots) % NumQuickSlots);
}

void UInventoryComponent::SetSlotData(int32 SlotIndex, const FQuickSlotData& NewData)
{
	if (SlotIndex == LightSourceSlotIndex || SlotIndex == ConsumableSlotIndex)
	{
		// Reserved for the auto-refreshed slots — see this function's own header comment. Refusing here
		// rather than letting a caller's data get silently overwritten by the next refresh tick anyway.
		UE_LOG(LogTemp, Warning, TEXT("%s: SetSlotData() refused for slot %d — reserved for auto-refresh"), *GetName(), SlotIndex);
		return;
	}

	ApplySlotData(SlotIndex, NewData);
}

void UInventoryComponent::ClearSlot(int32 SlotIndex)
{
	SetSlotData(SlotIndex, FQuickSlotData());
}

void UInventoryComponent::ApplySlotData(int32 SlotIndex, const FQuickSlotData& NewData)
{
	if (!QuickSlots.IsValidIndex(SlotIndex))
	{
		return;
	}

	const FQuickSlotData& OldData = QuickSlots[SlotIndex];
	const bool bUnchanged = OldData.SlotType == NewData.SlotType
		&& OldData.AssociatedActor == NewData.AssociatedActor
		&& OldData.ChargeCount == NewData.ChargeCount
		&& OldData.MaxChargeCount == NewData.MaxChargeCount
		&& OldData.DisplayName.EqualTo(NewData.DisplayName)
		&& FMath::IsNearlyEqual(OldData.FillFraction, NewData.FillFraction, 0.005f)
		&& FMath::IsNearlyEqual(OldData.SecondsRemaining, NewData.SecondsRemaining, 0.05f);

	if (bUnchanged)
	{
		return;
	}

	QuickSlots[SlotIndex] = NewData;
	OnInventoryUpdated.Broadcast();
}

void UInventoryComponent::RefreshLightSourceSlot()
{
	if (!IsValid(OwnerCharacter))
	{
		// Re-resolve rather than trusting this component's own BeginPlay() already having set it —
		// ABaseCharacter::InitializeLocalPlayerUI() forces a refresh (see its own comment) before
		// creating the HUD widget, which can fire via PossessedBy() — for the GameMode's initial pawn —
		// BEFORE this component's BeginPlay() has run at all. GetOwner() is valid from construction/
		// attachment time, long before any BeginPlay() runs, so this recovers immediately instead of
		// falling through to the "owner is gone" branch below on a false alarm.
		OwnerCharacter = Cast<ABaseCharacter>(GetOwner());
	}

	if (!IsValid(OwnerCharacter))
	{
		// The owning character has died/been destroyed (or, defensively, was never validly set) —
		// stop the auto-refresh timer instead of silently skipping just this one tick, so it can't keep
		// firing indefinitely against an owner that will never become valid again. EndPlay() (see its own
		// comment) is the primary guard against this timer outliving the COMPONENT itself; this covers the
		// narrower case where OwnerCharacter alone goes invalid while this component is still alive.
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(AutoRefreshTimerHandle);
		}
		return;
	}

	// Right hand preferred, same preference order PickUp() itself uses. Only a currently-lit item counts
	// as "the" light source — an extinguished/spent one shouldn't show as equipped light. Explicit
	// IsValid() on each hand independently of OwnerCharacter's own validity: EquippedRightHand/
	// EquippedLeftHand are raw AHolyItem* fields that can go stale if the held item itself is destroyed
	// out from under a still-valid character.
	AHolyItem* LightItem = nullptr;
	if (IsValid(OwnerCharacter->EquippedRightHand) && OwnerCharacter->EquippedRightHand->bIsLit)
	{
		LightItem = OwnerCharacter->EquippedRightHand;
	}
	else if (IsValid(OwnerCharacter->EquippedLeftHand) && OwnerCharacter->EquippedLeftHand->bIsLit)
	{
		LightItem = OwnerCharacter->EquippedLeftHand;
	}

	FQuickSlotData NewData;
	if (IsValid(LightItem))
	{
		NewData.SlotType = EQuickSlotType::LightSource;
		NewData.DisplayName = FText::FromString(LightItem->ItemType == EHolyItemType::Candle ? TEXT("Candle") : TEXT("Censer"));
		NewData.AssociatedActor = LightItem;
		NewData.FillFraction = LightItem->MaxFuel > 0.0f ? (LightItem->FuelRemaining / LightItem->MaxFuel) : 1.0f;
	}
	else
	{
		// Flashlight fallback — only ARedneckCharacter/ASiblingCharacter carry one.
		UFlashlightComponent* Flashlight = nullptr;
		if (ARedneckCharacter* Redneck = Cast<ARedneckCharacter>(OwnerCharacter))
		{
			Flashlight = Redneck->FlashlightComponent;
		}
		else if (ASiblingCharacter* Sibling = Cast<ASiblingCharacter>(OwnerCharacter))
		{
			Flashlight = Sibling->FlashlightComponent;
		}

		if (IsValid(Flashlight) && Flashlight->IsOn())
		{
			NewData.SlotType = EQuickSlotType::LightSource;
			NewData.DisplayName = FText::FromString(TEXT("Flashlight"));
			NewData.AssociatedActor = nullptr;
			NewData.FillFraction = Flashlight->MaxBattery > 0.0f ? (Flashlight->BatteryRemaining / Flashlight->MaxBattery) : 1.0f;
		}
		// Else: nothing lit at all — NewData stays default-constructed (Empty).
	}

	ApplySlotData(LightSourceSlotIndex, NewData);
}

void UInventoryComponent::RefreshConsumableSlot()
{
	FQuickSlotData NewData;

	// Same reasoning as RefreshLightSourceSlot()'s own IsValid(OwnerCharacter) guard: this runs from the
	// same auto-refresh timer tick, immediately after that function's own check — so if OwnerCharacter
	// died/was destroyed mid-tick, this call still needs its own guard rather than relying on the other
	// function having already cleared the timer (ClearTimer() only prevents FUTURE ticks, not this one
	// still in progress). Cast<>() on an invalid/garbage pointer is exactly the kind of unsafe dereference
	// that crashed RefreshLightSourceSlot() before its own checks were added.
	if (!IsValid(OwnerCharacter))
	{
		// Re-resolve rather than trusting BeginPlay() already having set it — see
		// RefreshLightSourceSlot()'s own comment on the InitializeLocalPlayerUI()-before-BeginPlay() race
		// this recovers from.
		OwnerCharacter = Cast<ABaseCharacter>(GetOwner());
	}

	if (!IsValid(OwnerCharacter))
	{
		ApplySlotData(ConsumableSlotIndex, NewData);
		return;
	}

	if (ARedneckCharacter* Redneck = Cast<ARedneckCharacter>(OwnerCharacter))
	{
		if (IsValid(Redneck->MoonshineComponent))
		{
			NewData.SlotType = EQuickSlotType::Consumable;
			NewData.DisplayName = FText::FromString(TEXT("Moonshine"));
			// No charges any more (see UMoonshineComponent's header comment) — ChargeCount/MaxChargeCount
			// stay at their default 0/0 ("not applicable"), FillFraction alone drives the HUD: full when
			// ready, draining across the active ram window, refilling across the cooldown.
			NewData.FillFraction = Redneck->MoonshineComponent->GetMoonshineFillFraction();
			NewData.SecondsRemaining = Redneck->MoonshineComponent->GetMoonshineSecondsRemaining();
		}
	}
	// Else: not the Redneck — NewData stays default-constructed (Empty).

	ApplySlotData(ConsumableSlotIndex, NewData);
}

bool UInventoryComponent::UseConsumableSlot()
{
	if (!QuickSlots.IsValidIndex(ConsumableSlotIndex) || QuickSlots[ConsumableSlotIndex].SlotType != EQuickSlotType::Consumable)
	{
		return false;
	}

	if (!IsValid(OwnerCharacter))
	{
		return false;
	}

	ARedneckCharacter* Redneck = Cast<ARedneckCharacter>(OwnerCharacter);
	if (!IsValid(Redneck) || !IsValid(Redneck->MoonshineComponent))
	{
		return false;
	}

	const bool bSucceeded = Redneck->MoonshineComponent->UseMoonshine();
	if (bSucceeded)
	{
		// Immediate refresh — same "don't wait for the next auto-refresh tick" reasoning as
		// RefreshLightSourceSlot()'s own call sites (PickUp()/Interact()/ThrowItem()).
		RefreshConsumableSlot();
	}

	return bSucceeded;
}

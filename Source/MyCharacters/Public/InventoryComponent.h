// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "InventoryComponent.generated.h"

class ABaseCharacter;

// Fixed roles per quick-slot index — NOT a generic "put anything anywhere" inventory. Slot 0 (quick-key
// "1") always reflects whichever light-providing item/flashlight is currently equipped; slot 1 ("2")
// always reflects the Redneck's Moonshine (meaningless on any other hero, just empty); slots 2-3 ("3"/"4")
// are free-form Utility/Quest slots a designer can populate via SetSlotData()/ClearSlot().
UENUM(BlueprintType)
enum class EQuickSlotType : uint8
{
	Empty,
	LightSource,
	Consumable,
	Utility
};

// Read-only snapshot of one quick-slot's current state, for the HUD to display — see EQuickSlotType's
// own comment for what each slot index means.
USTRUCT(BlueprintType)
struct FQuickSlotData
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Inventory")
	EQuickSlotType SlotType = EQuickSlotType::Empty;

	// What the HUD prints as this slot's label (e.g. "Candle", "Moonshine", "Flashlight").
	UPROPERTY(BlueprintReadOnly, Category = "Inventory")
	FText DisplayName;

	// The actor this slot currently represents, if any (e.g. the equipped AHolyItem) — purely for
	// HUD/Blueprint convenience (icon lookups, etc.); the component itself never dereferences this.
	UPROPERTY(BlueprintReadOnly, Category = "Inventory")
	TWeakObjectPtr<AActor> AssociatedActor;

	// Normalized [0, 1] resource fill for a bar-style HUD readout (fuel/battery remaining, boost time
	// remaining, etc.) — 1 means full/not applicable, 0 means empty. Ignored (treated as N/A) when
	// SlotType is Empty.
	UPROPERTY(BlueprintReadOnly, Category = "Inventory")
	float FillFraction = 1.0f;

	// Integer charge count for a charges-style HUD readout (e.g. Moonshine's CurrentCharges/MaxCharges)
	// — both 0 means "not applicable" (use FillFraction instead).
	UPROPERTY(BlueprintReadOnly, Category = "Inventory")
	int32 ChargeCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Inventory")
	int32 MaxChargeCount = 0;

	// Seconds left in whatever timed state this slot is currently in (e.g. Moonshine's active buff
	// window OR its subsequent cooldown — see UMoonshineComponent::GetMoonshineSecondsRemaining()), for
	// a numeric countdown HUD readout. 0 means "not applicable" (ready/no timer running) — a real
	// text-based fallback for FillFraction above, since PaintQuickSlot()'s bar rendering has a known,
	// unresolved rendering-pipeline issue (see that function's own comment) that MakeText draw calls
	// aren't affected by.
	UPROPERTY(BlueprintReadOnly, Category = "Inventory")
	float SecondsRemaining = 0.0f;
};

// Broadcast whenever any quick-slot's data actually changes (equipping/dropping the light source,
// Moonshine charges/fuel ticking, a designer calling SetSlotData()/ClearSlot() on a Utility slot) — the
// HUD binds to this to refresh its quick-slot bar instead of polling every frame itself. Fires with no
// per-slot data of its own (BlueprintCallable GetSlotData() below is how a handler reads the actual
// values) since a single refresh often touches multiple slots at once.
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnInventoryUpdated);

// Broadcast whenever ActiveSlotIndex changes via SelectSlot()/SelectNextSlot()/SelectPreviousSlot().
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnActiveSlotChanged, int32, NewIndex, int32, OldIndex);

// Every ABaseCharacter's quick-slot inventory (4 fixed-role slots — see EQuickSlotType) and active-slot
// selection (Number keys 1-4, mouse wheel). Slots 0/1 (Light Source/Consumable) are read-only from the
// outside: RefreshLightSourceSlot()/RefreshConsumableSlot() poll live game state (equipped item fuel,
// UMoonshineComponent charges) on a short repeating timer rather than needing every mutation call site
// across PickUp()/DropItem()/ThrowItem()/TryDrink() to explicitly notify this component. Slots 2/3
// (Utility) are the only ones SetSlotData()/ClearSlot() are meant for from outside code.
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class MYCHARACTERS_API UInventoryComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UInventoryComponent();

	static constexpr int32 NumQuickSlots = 4;

	UPROPERTY(BlueprintAssignable, Category = "Inventory")
	FOnInventoryUpdated OnInventoryUpdated;

	UPROPERTY(BlueprintAssignable, Category = "Inventory")
	FOnActiveSlotChanged OnActiveSlotChanged;

	// Currently highlighted slot, [0, NumQuickSlots). Purely a UI/selection concept today — it doesn't
	// change gameplay by itself (the actual equipped Light Source is still whatever's in
	// EquippedRightHand/EquippedLeftHand); a future "use active slot" action would read this.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Inventory")
	int32 ActiveSlotIndex = 0;

	// How often (in seconds) slots 0/1 (Light Source/Consumable) are refreshed from live game state.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory")
	float AutoRefreshInterval = 0.25f;

	// Returns a copy of SlotIndex's current data, or a default-constructed (Empty) FQuickSlotData if
	// SlotIndex is out of range.
	UFUNCTION(BlueprintCallable, Category = "Inventory")
	FQuickSlotData GetSlotData(int32 SlotIndex) const;

	// Selects SlotIndex directly (e.g. from a Number-key press) — clamped to [0, NumQuickSlots). No-op
	// (no broadcast) if SlotIndex is already the active one.
	UFUNCTION(BlueprintCallable, Category = "Inventory")
	void SelectSlot(int32 SlotIndex);

	// Wraps forward/backward through the 4 slots — bound to the mouse wheel.
	UFUNCTION(BlueprintCallable, Category = "Inventory")
	void SelectNextSlot();

	UFUNCTION(BlueprintCallable, Category = "Inventory")
	void SelectPreviousSlot();

	// Designer/quest hook for the Utility slots (index 2 or 3 — SlotIndex 0/1 are reserved for the
	// auto-refreshed Light Source/Consumable slots and refusing to overwrite them here rather than
	// silently getting stomped by the next auto-refresh tick).
	UFUNCTION(BlueprintCallable, Category = "Inventory")
	void SetSlotData(int32 SlotIndex, const FQuickSlotData& NewData);

	UFUNCTION(BlueprintCallable, Category = "Inventory")
	void ClearSlot(int32 SlotIndex);

	// Slot 0: whichever of EquippedRightHand/EquippedLeftHand is a lit AHolyItem (right hand preferred,
	// same preference order PickUp() itself uses), else the owning character's UFlashlightComponent (if
	// it's ARedneckCharacter/ASiblingCharacter) if currently on, else Empty. Runs automatically on
	// AutoRefreshTimerHandle's own cadence, but public so a call site that just changed the equipped light
	// source (e.g. ABaseCharacter::ThrowItem()) can force an immediate refresh instead of leaving the HUD
	// showing the just-thrown item for up to AutoRefreshInterval seconds.
	UFUNCTION(BlueprintCallable, Category = "Inventory")
	void RefreshLightSourceSlot();

	// Slot 1's "use" action: no-ops (returns false) unless the Consumable slot is actually populated
	// (EQuickSlotType::Consumable — currently only ever true for ARedneckCharacter's Moonshine) and
	// otherwise forwards to UMoonshineComponent::UseMoonshine(), which owns the real ready/active/
	// cooldown gating (bCanDrinkMoonshine) — this function is purely the "go through the item/inventory
	// system rather than calling the ability component directly" entry point ARedneckCharacter's Drink
	// key (or any future quick-slot "use" input) should call. Forces an immediate RefreshConsumableSlot()
	// on success so the HUD reflects the new active/cooldown state right away instead of waiting up to
	// AutoRefreshInterval seconds.
	UFUNCTION(BlueprintCallable, Category = "Inventory")
	bool UseConsumableSlot();

	// Slot 1: the owning character's UMoonshineComponent active/cooldown state (ARedneckCharacter only —
	// Empty on every other hero). Runs automatically on AutoRefreshTimerHandle's own cadence, same as
	// RefreshLightSourceSlot() above, but public for the same reason: ABaseCharacter::
	// InitializeLocalPlayerUI() calls both directly before creating the HUD widget, since that can fire
	// (via PossessedBy(), for the GameMode's initial pawn) before this component's own BeginPlay() has
	// had a chance to run — without an explicit call here, the HUD's one-time "pull current state on
	// construct" would read the still-default-Empty QuickSlots instead of the real Moonshine slot.
	UFUNCTION(BlueprintCallable, Category = "Inventory")
	void RefreshConsumableSlot();

protected:
	virtual void BeginPlay() override;

	// Clears AutoRefreshTimerHandle. This is the load-bearing fix for a real crash: the timer set in
	// BeginPlay() captures a raw `this` (this component) in its lambda — if this component (or its owner)
	// is destroyed while that timer is still pending (e.g. the owning character dies mid-game) and nothing
	// clears it, TimerManager can still fire that lambda later against memory Garbage Collection has since
	// reclaimed. That's a genuine use-after-free — calling a member function through a dangling `this` is
	// undefined behavior even before any IsValid() check inside the function body gets a chance to run — so
	// the IsValid(OwnerCharacter)/IsValid(EquippedRightHand)/IsValid(EquippedLeftHand) checks in
	// RefreshLightSourceSlot() alone could never have fully prevented this; only tearing the timer down
	// here, synchronously on destruction, does.
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	// Sets QuickSlots[SlotIndex] and broadcasts OnInventoryUpdated only if the new value actually
	// differs from the old one (FillFraction compared with a small tolerance — it changes continuously
	// while draining, and broadcasting every 0.01% tick would be pointless HUD churn).
	void ApplySlotData(int32 SlotIndex, const FQuickSlotData& NewData);

	UPROPERTY()
	TArray<FQuickSlotData> QuickSlots;

	UPROPERTY()
	TObjectPtr<ABaseCharacter> OwnerCharacter;

	FTimerHandle AutoRefreshTimerHandle;
};

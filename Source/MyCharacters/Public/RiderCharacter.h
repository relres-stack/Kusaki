// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "BaseCharacter.h"
#include "RiderCharacter.generated.h"

class UMountRiderComponent;
class AHolyItem;
class AHookPoint;
class URiderHookComponent;
class UResourceStockpileComponent;
class UInkVisionComponent;

// The Rider: gets the Holy Censer (gifted by the Priest — see StartingCenserClass), which creates Holy
// Protection the same as the Priest's Candles. Also the ONLY hero who can mount/ride an ABaseMonster —
// the entire mounting system lives in MountRiderComponent (see its own header comment); this class is a
// thin wrapper that owns the component and satisfies ABaseCharacter's virtual mount-query hooks
// (IsMounted()/GetMountedMonster()/Dismount()/OnKnockedOffMount()) by delegating to it.
UCLASS()
class MYCHARACTERS_API ARiderCharacter : public ABaseCharacter
{
	GENERATED_BODY()

public:
	ARiderCharacter();

	// The entire mounting system — see MountRiderComponent's own header comment. Public so Blueprint/
	// other systems can inspect mount state directly if needed, though ABaseCharacter's virtual
	// IsMounted()/GetMountedMonster() accessors are the normal way shared code queries it.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mounting")
	TObjectPtr<UMountRiderComponent> MountRiderComponent;

	virtual bool IsMounted() const override;
	virtual ABaseMonster* GetMountedMonster() const override;
	virtual void Dismount() override;
	virtual class UAnimSequence* GetMountedRidingAnimation() const override;

	// The Rider-exclusive hook grapple — see URiderHookComponent's own header comment.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rider Hook")
	TObjectPtr<URiderHookComponent> HookComponent;

	// "Чернильное зрение" — see UInkVisionComponent's own header comment. Rider-exclusive, same
	// "hero-specific ability component" precedent as HookComponent/OilReserve, not universal like
	// UProbeComponent/UHidingComponent.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ink Vision")
	TObjectPtr<UInkVisionComponent> InkVisionComponent;

	// Context-sensitive dispatcher for F, bound to BOTH the "Mount" action mapping and the direct
	// BindKey(EKeys::F) fallback in SetupPlayerInputComponent() (previously both pointed straight at
	// MountRiderComponent->TryMount()): hanging on a hook -> HookComponent->DropFromHook(); mounted and
	// aiming at an AHookPoint (via TraceAimTarget(), the same trace TryMount() itself uses) ->
	// HookComponent->TryGrapple(); mounted but not aiming at a hook -> no-op (matches the old behavior of
	// F doing nothing while already mounted); not mounted -> MountRiderComponent->TryMount(), unchanged.
	UFUNCTION(BlueprintCallable, Category = "Mounting")
	void OnMountOrHookPressed();

	// Optional starting Censer class — if set, BeginPlay() spawns one and immediately picks it up (the
	// Priest's in-fiction gift), so a level doesn't need a Censer pre-placed for the Rider to start
	// equipped. Left unset (None) by default: purely a designer convenience hook, not a hard requirement.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rider")
	TSubclassOf<AHolyItem> StartingCenserClass;

	// Censer oil reserve — see UResourceStockpileComponent's own header comment. Spend/refill logic is a
	// separate future task; for now just holds the starting amount and feeds RefreshOilReserveSlot()'s
	// HUD readout.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rider")
	TObjectPtr<UResourceStockpileComponent> OilReserve;

	// Transfers up to OilReserve's own current amount into the equipped Censer's fuel tank (AHolyItem::
	// FuelRemaining, capped at MaxFuel) 1:1, whichever hand it's in. Silent no-op (no debug message) if
	// the reserve is empty, no Censer is equipped, or the tank is already full — three different reasons
	// to do nothing, not worth a message each. Bound to R in SetupPlayerInputComponent(). Doesn't touch
	// bIsLit/Extinguish() — refilling never lights or puts out the Censer, only its fuel level.
	UFUNCTION(BlueprintCallable, Category = "Rider")
	void RefillCenser();

	// One toggle button between "extinguished, on the belt" and "lit, in hand": if the equipped Censer is
	// currently out, lights it (LightUp() itself already refuses if FuelRemaining <= 0 — no need for a
	// separate fuel check here); if lit, puts it out (Extinguish()). Both halves already move the item
	// between the hand/belt sockets on their own — this only decides which one to call. Bound to C in
	// SetupPlayerInputComponent().
	UFUNCTION(BlueprintCallable, Category = "Rider")
	void ToggleCenser();

	// Minimum AHolyItem::FuelRemaining the equipped Censer must have for TryHolyExplosion() to activate —
	// the whole tank is spent unconditionally on resolution (see ResolveHolyExplosion()), not proportional
	// to this minimum.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rider|Holy Explosion")
	float HolyExplosionMinFuel = 5.0f;

	// Radius (in units), centered on the Rider's landing location, ResolveHolyExplosion() kills every live
	// ABaseMonster within — deliberately larger than AHolyItem::CenserPushbackRadius (200), a real AoE nuke
	// rather than the Censer's ordinary gentle pushback.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rider|Holy Explosion")
	float HolyExplosionRadius = 500.0f;

	// Seconds between TryHolyExplosion() activating and ResolveHolyExplosion() actually resolving the
	// blast — the "windup": the Rider is launched into the air (see HolyExplosionJumpVelocity) and comes
	// back down during this window before the explosion goes off.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rider|Holy Explosion")
	float HolyExplosionDelay = 0.8f;

	// Upward launch speed (units/sec) TryHolyExplosion() gives the Rider via LaunchCharacter() on
	// activation.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rider|Holy Explosion")
	float HolyExplosionJumpVelocity = 500.0f;

	// Activates the Holy Explosion: lights the equipped Censer (if not already lit), launches the Rider
	// upward, and schedules ResolveHolyExplosion() after HolyExplosionDelay seconds. Requires an equipped
	// Censer with at least HolyExplosionMinFuel remaining; silent no-op while mounted, panicking, or
	// already charging (HolyExplosionTimerHandle active). Bound to X in SetupPlayerInputComponent().
	UFUNCTION(BlueprintCallable, Category = "Rider|Holy Explosion")
	void TryHolyExplosion();

protected:
	virtual void BeginPlay() override;
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;
	virtual void OnKnockedOffMount(AActor* DamageCauser) override;

private:
	// Pushes OilReserve's current/max amount into the HUD's Utility slot (index 2, key "3") via
	// UInventoryComponent::SetSlotData() — same reasoning as APriestCharacter::RefreshCandleStockpileSlot().
	// Called once from BeginPlay() for now; future spend/refill tasks will call it again after OilReserve
	// actually changes.
	void RefreshOilReserveSlot();

	// Called by the timer TryHolyExplosion() sets — kills every live ABaseMonster within HolyExplosionRadius
	// of the Rider's current (landed) location via UCenserDamageType, then empties and extinguishes
	// PendingHolyExplosionCenser regardless of how much fuel it actually had left.
	void ResolveHolyExplosion();

	// Set by TryHolyExplosion(), consumed and cleared by ResolveHolyExplosion() — the Censer whose fuel
	// tank the pending explosion will empty. TWeakObjectPtr since the Censer could in principle be dropped/
	// destroyed during the windup.
	TWeakObjectPtr<class AHolyItem> PendingHolyExplosionCenser;

	// Guards TryHolyExplosion() against re-activating while an explosion is already charging.
	FTimerHandle HolyExplosionTimerHandle;

	// Thin wrapper bound to V in SetupPlayerInputComponent() — same "wrapper, not a direct component-
	// method bind" pattern as APriestCharacter::OnVisionFireTogglePressed().
	void OnInkVisionTogglePressed();
};

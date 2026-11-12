// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "BaseCharacter.h"
#include "RedneckCharacter.generated.h"

class UMoonshineComponent;
class UFlashlightComponent;
class USpotLightComponent;
class UHunterFocusComponent;
class URageFallComponent;
class UMoonshineScentComponent;
class ADrunkNPC;

// The Redneck: carries a Flashlight (ordinary light — see UFlashlightComponent's own comment on why this
// provides vision only and never repels monsters, unlike the Priest's Candle/Rider's Censer), a
// UMoonshineComponent ("Moonshine & Rage Dash": drinking action, cooldown gate, temporary speed boost
// plus a ram-on-hit window — see that class), and a UHunterFocusComponent ("Hunter's Focus": brief
// slow-motion + a step-count readout to whatever's ahead — see that class).
UCLASS()
class MYCHARACTERS_API ARedneckCharacter : public ABaseCharacter
{
	GENERATED_BODY()

public:
	ARedneckCharacter();

	// Drinking action/cooldown/speed-boost/ram-on-hit — see UMoonshineComponent's own header comment.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Redneck")
	TObjectPtr<UMoonshineComponent> MoonshineComponent;

	// Slow-motion focus + step-count readout — see UHunterFocusComponent's own header comment.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Redneck")
	TObjectPtr<UHunterFocusComponent> HunterFocusComponent;

	// Post-Rage-Dash stumble (wall hit / clean miss) — see URageFallComponent's own header comment.
	// Entirely outside MoonshineComponent; only triggered from TryHijackAttackPress() right after
	// PerformRageDash().
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Redneck")
	TObjectPtr<URageFallComponent> RageFallComponent;

	// Post-buff "hangover scent" trail — periodically gives away the Redneck's position to nearby monsters
	// for a while after the Moonshine buff wears off, even standing still, whether or not a Rage Dash ever
	// happened during it. See UMoonshineScentComponent's own header comment — it watches
	// MoonshineComponent->bIsMoonshineActive itself (via its own TickComponent()) and starts on its own on
	// the true -> false transition; NOT triggered from TryHijackAttackPress()/the dash, unlike
	// RageFallComponent above, which stays tied to the dash itself.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Redneck")
	TObjectPtr<UMoonshineScentComponent> MoonshineScentComponent;

	// Battery/toggle logic — see UFlashlightComponent's own header comment. The actual light is
	// FlashlightLight below; this component is assigned a reference to it in the constructor.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Redneck")
	TObjectPtr<UFlashlightComponent> FlashlightComponent;

	// The actual light — created here (not by UFlashlightComponent itself; see its header comment on
	// why) and attached to the mesh.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Redneck")
	TObjectPtr<USpotLightComponent> FlashlightLight;

	// Bone on the mesh the flashlight attaches to — see ABaseCharacter::RightHandSocket's own comment on
	// why this is the "hand_r" BONE name, not the old "Socket_Hand_R" custom socket (which doesn't exist
	// on the SKM_Manny_Simple mesh this character actually uses).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Redneck")
	FName FlashlightSocket = TEXT("hand_r");

	// True for the full duration of an active Moonshine Rage Dash (UMoonshineComponent::
	// bIsMoonshineActive) — see ABaseCharacter::IsImmuneToMonsterCatch()'s own comment on how
	// ABaseMonster::CheckForPlayer() uses this to skip the kill entirely and let the ram collision
	// (UMoonshineComponent::OnOwnerCapsuleHit()) stun/knock back the monster instead.
	virtual bool IsImmuneToMonsterCatch() const override;

	// "5 STEPS TO TARGET"/"SAFE STEPS: 5" while HunterFocusComponent->bIsFocusActive, empty otherwise —
	// see ABaseCharacter::GetHunterFocusPromptText()'s own comment on how BuildInteractionPromptText()
	// uses this.
	virtual FString GetHunterFocusPromptText() const override;

	// Forwards to HunterFocusComponent->SetMentalTrailActive() — see ABaseCharacter::
	// OnEyesClosedChanged()'s own comment on why closing eyes transitions Hunter's Focus's step markers
	// into their glowing "Mental Trail" state instead of hiding/destroying them.
	virtual void OnEyesClosedChanged(bool bClosed) override;

	// Overrides ABaseCharacter::TryHijackAttackPress() — "Bulls Charge" (Таран), Tier 2 of
	// UMoonshineComponent's two-tier system (see that class's own header comment): while aiming (RMB
	// held, bIsAiming true) AND MoonshineComponent->bIsMoonshineActive is ALREADY true (activated
	// separately, from the inventory consumable slot — this never activates or spends it), the attack
	// press (LMB) triggers MoonshineComponent->PerformRageDash() — a deliberate directional lunge —
	// instead of ThrowItem()'s normal throw. With RMB not held, OR with the buff not active, this returns
	// false and ThrowItem() runs its own normal aimed throw/attack logic untouched — no dash without the
	// buff already running.
	virtual bool TryHijackAttackPress() override;

	// Hilltop binoculars beat: if an unused ABinocularsPickup is within its own InteractRadius AND this
	// character isn't trapped (checked here, before ever touching the binoculars, so E while caught in a
	// Bear Trap near the binoculars still only ever drives MashTrapEscape — see ABaseCharacter::Interact()'s
	// own bIsTrapped guard, never reached in that case), calls its LookThroughBinoculars() and returns
	// without reaching Super::Interact() at all. Otherwise falls through to Super::Interact() unchanged —
	// ordinary Candle/item pickup-or-drop still works exactly as before everywhere the binoculars aren't
	// nearby (or have already been used).
	virtual void Interact() override;

	// The ADrunkNPC currently slung on the Redneck's back, or nullptr if not carrying one — see
	// ADrunkNPC's own header comment for the full "спаивание НПС-собутыльников" cycle. Set by
	// Interact() (TryGetDrunk() succeeds), cleared by TryHijackAttackPress() (ThrowTo()). Only one at a
	// time: Interact() won't pick up a second one while this is already set.
	UPROPERTY()
	TObjectPtr<ADrunkNPC> CarriedNPC;

	// Redneck's own keys for the on-screen controls list — see ABaseCharacter::GetControlHints().
	virtual void GetControlHints(TArray<FControlHint>& OutHints) const override;

protected:
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

private:
	// Bound to a dedicated "Flashlight" action/T key — thin wrapper so SetupPlayerInputComponent() can
	// bind a UFUNCTION directly without exposing FlashlightComponent's own Toggle() signature concerns.
	UFUNCTION()
	void OnFlashlightPressed();

	// Bound directly to F — same wrapper reasoning as OnFlashlightPressed() above. F is reserved
	// exclusively for Hunter's Focus (this project's "Прощупывание"/Tactile Search mechanic) — never
	// repurpose it for anything else.
	UFUNCTION()
	void OnHunterFocusPressed();

	// Bound to a dedicated "UseMoonshine" action/Q key — Tier 1 of UMoonshineComponent's two-tier system
	// (see that class's own header comment): activates ONLY the buff, routed through the Consumable
	// quick slot (UInventoryComponent::UseConsumableSlot()) rather than calling
	// MoonshineComponent->UseMoonshine() directly, same "drinking is the item's use action" reasoning the
	// old V binding used — see UseConsumableSlot()'s own comment. Q is also "MeleeSwing" in
	// ABaseCharacter::SetupPlayerInputComponent() (for a held Censer); both are bound and each self-gates
	// (SwingHeldCenser() no-ops without an equipped Censer, which Redneck never carries by default), same
	// "shared key, each function guards itself" pattern as E (Interact/MashTrapEscape) or Space
	// (Jump/Breathing).
	UFUNCTION()
	void OnUseMoonshinePressed();
};

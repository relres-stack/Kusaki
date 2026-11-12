// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MoonshineComponent.generated.h"

class ABaseMonster;

// ARedneckCharacter's "Moonshine & Rage Dash"/"Bulls Charge" mechanic (design doc: Самогон и Таран), a
// two-tier system:
//
//   Tier 1 — the BUFF (UseMoonshine(), from the inventory consumable slot — see
//   UInventoryComponent::UseConsumableSlot()): a short, high-risk window (bIsMoonshineActive) that boosts
//   the owning character's SprintSpeed and turns on OnOwnerCapsuleHit()'s light passive ram-on-touch
//   (see its own comment) for MoonshineDuration seconds. Drinking ONLY starts this window — it never
//   moves or launches the character by itself.
//
//   Tier 2 — the AIMED DASH (PerformRageDash(), called ONLY from
//   ARedneckCharacter::TryHijackAttackPress() — the RMB (Aim) + LMB (Attack) combo, and ONLY while
//   bIsMoonshineActive is already true): a deliberate, powerful directional lunge along the aim
//   direction. Aiming/attacking NEVER call UseMoonshine() themselves — RMB+LMB only ever READS whether
//   the buff happens to already be active; with the buff inactive, RMB+LMB falls through to an ordinary
//   aimed attack (ThrowItem()) instead, no dash.
//
// No charges/regen on the buff — a single ready/cooldown gate (bCanDrinkMoonshine) — deliberately
// simpler than the mechanic's earlier prototype, since the punishing part of "Rage Dash" is that missing
// costs the full MoonshineDuration + MoonshineCooldown before it's available again, not a limited
// resource. Attach specifically to ARedneckCharacter (see its constructor) — a self-contained
// UActorComponent rather than logic baked into ABaseCharacter, so no other hero carries any
// moonshine-specific state.
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class MYCHARACTERS_API UMoonshineComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMoonshineComponent();

	// Seconds the buff (speed boost + ram-on-hit) lasts after a successful UseMoonshine().
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Moonshine")
	float MoonshineDuration = 5.0f;

	// Seconds the drink action is unavailable (bCanDrinkMoonshine stays false) AFTER the buff above
	// expires — not concurrent with MoonshineDuration, per the design doc ("after 5 seconds, the buff
	// expires... a Cooldown timer begins"), so a full miss costs MoonshineDuration + MoonshineCooldown
	// before the player can try again.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Moonshine")
	float MoonshineCooldown = 8.0f;

	// Общий конечный запас самогона в абстрактных единицах ("бутылка" = 100 по умолчанию) —
	// расходуется И на собственный Tier 1 буф (PersonalDrinkCost за глоток), И на угощение
	// НПС-приманки (см. TryConsumeForNPC() ниже, отдельная задача "НПС-приманка" читает этот же
	// запас). Один общий карман, не два разных — по прямому решению пользователя (27.08.2026):
	// отдать НПС значит меньше достанется (или вообще не останется) самому Реднеку, и наоборот.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Moonshine|Resource")
	float MoonshineStock = 100.0f;

	// Сколько единиц запаса тратит один собственный глоток (UseMoonshine()). Стартовая цифра —
	// подобрать в PIE, не финальный баланс (как и большинство подобных чисел в проекте).
	UPROPERTY(EditAnywhere, Category = "Moonshine|Resource")
	float PersonalDrinkCost = 25.0f;

	// Fractional speed bonus applied to the owning character's SprintSpeed (ABaseCharacter — NOT the live
	// GetCharacterMovement()->MaxWalkSpeed directly) for MoonshineDuration after a successful drink — 0.5
	// means +50% (SprintSpeed * 1.5). If the character is already sprinting when the buff starts/ends,
	// UseMoonshine()/EndMoonshine() also push the boosted/reverted value onto the live MaxWalkSpeed
	// immediately — see their own comments — so the speed change is felt right away instead of waiting
	// for the next Sprint toggle.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Moonshine")
	float MoonshineSpeedBoost = 0.5f;

	// True while UseMoonshine() is available — false from the moment a drink succeeds until
	// MoonshineDuration + MoonshineCooldown seconds later. Checked by UseMoonshine() itself; exposed
	// separately so the HUD/Blueprint can read readiness without having to infer it from the timers.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Moonshine")
	bool bCanDrinkMoonshine = true;

	// True for MoonshineDuration seconds after a successful drink: the owning character's SprintSpeed is
	// boosted AND any monster/obstacle its capsule collides with while moving (see OnOwnerCapsuleHit())
	// gets a light passive ram instead of just blocking movement. This flag by itself never moves the
	// character — the deliberate, powerful directional lunge (PerformRageDash()) only ever fires from the
	// RMB (Aim) + LMB (Attack) combo, and only while this is already true. High risk: nothing about this
	// flag makes the PLAYER any less vulnerable if the window closes without using it.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Moonshine")
	bool bIsMoonshineActive = false;

	// Push/stun applied via ABaseMonster::ApplyKnockback()/Stun() to whatever this rams — via the light
	// passive OnOwnerCapsuleHit() bump, or a PerformRageDash() collision, both funnel through the same
	// OnOwnerCapsuleHit() handler and so share these same values — while bIsMoonshineActive is true.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Moonshine|Ram")
	float RamKnockbackForce = 1200.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Moonshine|Ram")
	float RamKnockbackUpwardForce = 200.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Moonshine|Ram")
	float RamStunDuration = 2.0f;

	// Damage applied via UGameplayStatics::ApplyDamage() to whatever this rams that ISN'T an ABaseMonster
	// (a destructible/obstacle actor — a door, a breakable prop) while bIsMoonshineActive is true — see
	// OnOwnerCapsuleHit()'s own comment. No dedicated destructible class exists in this project yet; this
	// is a plain UDamageType hit so any future Door/obstacle actor just needs its own TakeDamage()
	// override to react to a charge. Same value whether the hit came from the light passive bump or a
	// deliberate PerformRageDash() collision.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Moonshine|Ram")
	float RamDamage = 25.0f;

	// Horizontal launch speed (units/sec) PerformRageDash() propels the owning character at, along their
	// current aim direction — see that function's own comment.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Moonshine|Ram")
	float DashLaunchSpeed = 1800.0f;

	// Upward component (units/sec) added to PerformRageDash()'s launch — same "pop" reasoning as
	// RamKnockbackUpwardForce, so the dash doesn't skim flush along the ground.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Moonshine|Ram")
	float DashLaunchUpwardSpeed = 100.0f;

	// Проигрывается один раз (не зациклено) поверх обычного AnimBP-движения в момент
	// самого рывка PerformRageDash() — удерживающий "боднул рогом" кадр, снятый через
	// PlayAnimation()/AnimationSingleNode (см. её собственный комментарий), не Montage.
	// nullptr по умолчанию — если не назначить, рывок остаётся как сейчас, только
	// физическим, без анимации.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Moonshine|Ram")
	class UAnimSequence* RageDashAnimation = nullptr;

	// Tier 1 — activates ONLY the buff: no-ops (and returns false) if bCanDrinkMoonshine is false. On
	// success, boosts the owning character's SprintSpeed by MoonshineSpeedBoost and sets
	// bIsMoonshineActive for MoonshineDuration seconds, then starts MoonshineCooldown before
	// bCanDrinkMoonshine goes back to true. Called from the inventory consumable slot (see
	// UInventoryComponent::UseConsumableSlot()) — NEVER from aiming or attacking (RMB/LMB); those only
	// ever READ bIsMoonshineActive (see ARedneckCharacter::TryHijackAttackPress()) and never call this.
	UFUNCTION(BlueprintCallable, Category = "Moonshine")
	bool UseMoonshine();

	// Tier 2 — the deliberate directional lunge: launches the owning character forward along their
	// current aim direction (GetControlRotation(), flattened) at DashLaunchSpeed. Called ONLY from
	// ARedneckCharacter::TryHijackAttackPress() — the RMB (Aim) + LMB (Attack) combo — and ONLY once that
	// function has already confirmed bIsMoonshineActive is true; this function still checks it again
	// defensively and no-ops if the buff somehow isn't active. Whatever the resulting launch physically
	// collides with is handled by the SAME OnOwnerCapsuleHit() the passive light bump already uses (same
	// RamDamage/RamKnockbackForce/RamStunDuration either way) — this function itself only supplies the
	// forward momentum, not any damage/knockback logic of its own.
	UFUNCTION(BlueprintCallable, Category = "Moonshine")
	void PerformRageDash();

	// Роняет случайное количество из общего запаса (между MinAmount и тем, что реально осталось
	// — если в баке меньше MaxAmount, забирает всё до нуля, не больше) — используется задачей
	// "НПС-приманка", когда Реднек угощает НПС и заранее не знает, сколько тот выпьет. Возвращает
	// false и OutAmountConsumed=0, если запас уже пуст (нечего наливать).
	UFUNCTION(BlueprintCallable, Category = "Moonshine|Resource")
	bool TryConsumeForNPC(float MinAmount, float MaxAmount, float& OutAmountConsumed);

	// Fraction, [0, 1], the HUD's quick-slot fill bar reads: counts down from 1 over MoonshineDuration
	// while bIsMoonshineActive, then counts back up from 0 to 1 over MoonshineCooldown while on cooldown,
	// pinned at 1 once ready to drink again.
	UFUNCTION(BlueprintCallable, Category = "Moonshine")
	float GetMoonshineFillFraction() const;

	// Seconds remaining in whichever timer is currently running — the active buff's remaining duration
	// while bIsMoonshineActive, or the cooldown's remaining time otherwise; 0 once ready to drink again.
	// What UInventoryComponent::RefreshConsumableSlot() feeds FQuickSlotData::SecondsRemaining — see that
	// field's own comment for why the HUD needs this numeric form alongside GetMoonshineFillFraction().
	UFUNCTION(BlueprintCallable, Category = "Moonshine")
	float GetMoonshineSecondsRemaining() const;

protected:
	virtual void BeginPlay() override;

private:
	// Ends the buff — timer callback for MoonshineTimerHandle, fired MoonshineDuration seconds after a
	// successful UseMoonshine(). Reverts SprintSpeed (and the live MaxWalkSpeed if still sprinting),
	// clears bIsMoonshineActive, and starts MoonshineCooldown before bCanDrinkMoonshine goes back to true
	// (see EndCooldown()).
	void EndMoonshine();

	// Timer callback for CooldownTimerHandle, fired MoonshineCooldown seconds after EndMoonshine() — the
	// only place bCanDrinkMoonshine is set back to true.
	void EndCooldown();

	// Bound (in BeginPlay()) to the owning character's capsule OnComponentHit — the "light passive bump
	// while moving" ram (Tier 1 of the buff — see this class's own header comment): while
	// bIsMoonshineActive, a blocking capsule hit rams whatever it struck instead of just stopping the
	// player's movement like an ordinary collision would, whether that hit came from ordinary
	// walking/sprinting OR from PerformRageDash()'s deliberate launch (Tier 2) — both funnel through
	// here. Two cases: an ABaseMonster gets the existing monster treatment (ApplyKnockback() + Stun());
	// anything else (a destructible/obstacle actor — doors, breakable props, no dedicated class for
	// these exists in this project yet) takes RamDamage via UGameplayStatics::ApplyDamage() and, if its
	// hit component is simulating physics, a matching knockback impulse — see RamDamage's own comment.
	UFUNCTION()
	void OnOwnerCapsuleHit(UPrimitiveComponent* HitComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit);

	// Возвращает мешу управление AnimBP после RageDashAnimation — таймер-колбэк,
	// см. PerformRageDash()'s own comment.
	void RestoreAnimationBlueprintControl();

	// The owning character's SprintSpeed as of just before the most recent UseMoonshine() boosted it —
	// what EndMoonshine() restores back to.
	float PreBoostSprintSpeed = 0.0f;

	FTimerHandle MoonshineTimerHandle;
	FTimerHandle CooldownTimerHandle;
	FTimerHandle RageDashAnimTimerHandle;
};

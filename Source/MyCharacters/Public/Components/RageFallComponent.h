// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "RageFallComponent.generated.h"

class ABaseMonster;
class UAnimMontage;

// ARedneckCharacter's "заваливание после Тарана" (stumble-after-Rage-Dash) mechanic. Entirely OUTSIDE
// UMoonshineComponent (a stable, do-not-touch mechanic — see CLAUDE.md's "Реестр механик") — this
// component only ever reacts to ARedneckCharacter::TryHijackAttackPress() already having called
// MoonshineComponent->PerformRageDash(), and listens on the SAME owner capsule OnComponentHit multicast
// delegate UMoonshineComponent::OnOwnerCapsuleHit() is already subscribed to, via its own independent
// AddDynamic() subscription — never touches UMoonshineComponent's own handler or state.
//
// Flow: StartDashResolutionWindow() (called right after PerformRageDash()) arms a short GraceWindow. If
// OnCapsuleHit() fires an ABaseMonster hit within that window, nothing happens (existing monster ram/stun
// logic in UMoonshineComponent already handles it) — the window is just disarmed. If it fires a hit
// against anything else (a wall/obstacle), the Redneck stumbles: movement disabled + WallHitDamage, for
// StumbleDuration seconds. A clean miss (the window times out with no hit at all) has NO consequence —
// deliberately NOT a stumble: DisableMovement() while airborne (falling off a ledge/jump mid-dash) froze
// the character mid-air, so a miss now just lets the dash's own momentum/gravity play out normally.
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class MYCHARACTERS_API URageFallComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	URageFallComponent();

	// Seconds after StartDashResolutionWindow() to wait for a capsule hit before treating the dash as a
	// clean miss (BeginStumble(/*bApplyDamage=*/false)).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rage Fall")
	float GraceWindow = 0.35f;

	// Seconds the owning character's movement stays disabled once a stumble begins.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rage Fall")
	float StumbleDuration = 2.0f;

	// Damage applied via UGameplayStatics::ApplyDamage() when the dash stumbles into a wall/obstacle.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rage Fall")
	float WallHitDamage = 15.0f;

	// True for StumbleDuration seconds after a wall-hit stumble begins.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rage Fall")
	bool bIsStumbling = false;

	// Optional stumble animation — played in BeginStumble() if set, via the owner's
	// USkeletalMeshComponent::GetAnimInstance()->Montage_Play(). TODO: no montage assigned yet; the
	// stumble is currently silent/pose-less beyond the movement lock.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rage Fall")
	TObjectPtr<UAnimMontage> StumbleMontage = nullptr;

	// Arms the GraceWindow resolution timer — call immediately after MoonshineComponent->PerformRageDash()
	// (see ARedneckCharacter::TryHijackAttackPress()). No-ops (re-arms cleanly) if called again before the
	// previous window resolved; harmless since OnCapsuleHit()/the timeout each only ever fire once per
	// window (bIsAwaitingDashResult guards both).
	UFUNCTION(BlueprintCallable, Category = "Rage Fall")
	void StartDashResolutionWindow();

protected:
	virtual void BeginPlay() override;

private:
	// Bound (in BeginPlay()) to the owning character's capsule OnComponentHit — same capsule
	// UMoonshineComponent::OnOwnerCapsuleHit() is already bound to, via this component's own separate
	// AddDynamic() subscription (multicast delegate, so both handlers run independently). Ignored entirely
	// unless bIsAwaitingDashResult is armed (i.e. this isn't ordinary walking collision).
	UFUNCTION()
	void OnCapsuleHit(UPrimitiveComponent* HitComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit);

	// Timer callback for GraceWindowTimerHandle — fires GraceWindow seconds after
	// StartDashResolutionWindow() if OnCapsuleHit() hasn't already disarmed bIsAwaitingDashResult. A clean
	// miss: just disarms the window, no stumble, no damage, no message — see this class's own header
	// comment on why (mid-air freeze bug).
	void OnGraceWindowExpired();

	// Wall-hit stumble entry point — called only from OnCapsuleHit()'s non-monster branch. No-ops if
	// already stumbling (no re-trigger/no stacking).
	void BeginStumble();

	// Timer callback for StumbleTimerHandle — restores movement and clears bIsStumbling.
	void EndStumble();

	// True between StartDashResolutionWindow() and either OnCapsuleHit() or OnGraceWindowExpired()
	// resolving it — gates OnCapsuleHit() so ordinary walking collisions (dash not in progress) are
	// ignored.
	bool bIsAwaitingDashResult = false;

	FTimerHandle GraceWindowTimerHandle;
	FTimerHandle StumbleTimerHandle;
};

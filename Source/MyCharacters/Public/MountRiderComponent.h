// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MountRiderComponent.generated.h"

class ABaseMonster;
class ABaseCharacter;
class UAnimSequence;

// The entire mounting SYSTEM, isolated here so ABaseCharacter itself never needs to know how mounting
// works — only ARiderCharacter carries one of these, and only ARiderCharacter's SetupPlayerInputComponent()
// binds the "Mount" action/F key to TryMount() at all, so no other hero can ever execute a mount. Operates
// entirely on its GetOwner() (cast to ACharacter/ABaseCharacter) for every actor-level effect (attachment,
// collision, camera, equipped-item physics) since a UActorComponent has no transform/collision of its own.
//
// Detection is unified with the aiming/crosshair system (see ABaseCharacter::TraceAimTarget()) rather than
// running its own independent sweep: TryMount() mounts whatever the crosshair is currently aimed at (the
// same trace that turns the HUD dot green over a mountable ABaseMonster — see UCrosshairWidget/
// UpdateAimCrosshair()), so "mount when the crosshair target is valid" is literally true, not just a
// design intention.
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class MYCHARACTERS_API UMountRiderComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMountRiderComponent();

	// True while the owning character is riding a mounted ABaseMonster — ARiderCharacter::IsMounted()
	// reads this to satisfy ABaseCharacter's virtual accessor.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mounting")
	bool bIsMounted = false;

	// The monster currently being ridden, or nullptr if not mounted. Set by TryMount(), cleared by
	// Dismount().
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mounting")
	TObjectPtr<ABaseMonster> MountedMonster;

	// Max distance (in units) between the owning character and a crosshair-targeted monster that
	// CanMountMonster() allows a mount at — a monster the crosshair is aimed at but is simply too far
	// away (still within the crosshair's own AimTraceDistance) is refused here, not by the aim trace
	// itself. Within the requested 400-500 range so players don't have to nearly overlap capsules.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mounting")
	float MountDistanceThreshold = 450.0f;

	// Buffer (in units) ABOVE MountedMonster's own GetScaledCapsuleHalfHeight() that TryMount() sits the
	// rider at. Deliberately computed from the monster's capsule height (varies per monster size) rather
	// than a single flat offset. Applied via SetActorRelativeLocation() relative to GetMesh() itself (no
	// socket) — attaching to a SPECIFIC BONE socket (e.g. "Spine_03") and applying a flat Z offset
	// relative to THAT bone's local axes is what previously put the rider sideways on the ground: an
	// individual bone's local space can be tilted/rotated arbitrarily relative to world up, unlike the
	// mesh component's own root, whose local Z stays world-up-aligned even though its yaw differs from
	// the capsule's (the standard humanoid mesh-to-capsule offset).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mounting")
	float MountRiderHeightBuffer = 30.0f;

	// Fallback flat height offset (in units) used only if MountedMonster's GetCapsuleComponent() is
	// somehow invalid at mount time (shouldn't happen for a normal ACharacter, but avoids computing off
	// a null capsule).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mounting")
	float MountFallbackFixedHeightOffset = 170.0f;

	// Outward (horizontal) speed (units/sec) LaunchCharacter() gives the rider when HandleKnockedOffMount()
	// forces a dismount — stronger than Dismount()'s own default backward launch (300), since getting hit
	// off a mount should read as a harder, more sudden knock-off than a voluntary dismount.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mounting")
	float MountKnockOffForce = 600.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mounting")
	float MountKnockOffUpwardForce = 250.0f;

	// Looped riding pose for the whole duration of the mount — consumed by ABP_Unarmed's AnimGraph via
	// ABaseCharacter::GetMountedRidingAnimation() (a Sequence Player + Blend Poses by Bool gated on
	// IsMounted()), NOT applied via PlayAnimation()/SetAnimationMode() anymore — that fully disabled the
	// AnimBP (and everything built on top of it, e.g. the Censer swing arm pose) for the whole mount.
	// Left nullptr = no mounted pose (current AnimGraph branch just falls through to normal locomotion).
	// Assign in BP defaults on UMountRiderComponent (e.g. BP_Rider).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mounting")
	TObjectPtr<UAnimSequence> MountedRidingAnimation = nullptr;

	// Attempts to mount whatever the owning character's crosshair (ABaseCharacter::TraceAimTarget()) is
	// currently aimed at — see this class's own header comment for why detection is unified with aiming
	// rather than run as an independent sweep. Bound to the "Mount" action/F key by
	// ARiderCharacter::SetupPlayerInputComponent(). No-op if already mounted, if the owner isn't a valid
	// ABaseCharacter, or if CanMountMonster() refuses the crosshair's current target — see its own
	// [Mount Attempt] on-screen/log diagnostic for exactly why.
	UFUNCTION(BlueprintCallable, Category = "Mounting")
	void TryMount();

	// Evaluates every condition required to mount Monster from DistanceToMonster units away, and returns
	// true only if ALL of them pass: owner not Panicking, owner not Trapped (Bear Trap), Monster
	// valid/not pending kill, Monster->Health > 0, Monster->bCanBeMounted, !Monster->bIsBeingRidden,
	// Monster->GetMesh() valid, and DistanceToMonster <= MountDistanceThreshold. OutReason is always
	// populated — "OK" on success, or every failing condition on failure (comma-separated, not just the
	// first one found).
	UFUNCTION(BlueprintCallable, Category = "Mounting")
	bool CanMountMonster(const ABaseMonster* Monster, float DistanceToMonster, FString& OutReason) const;

	// Detaches from MountedMonster, restores the owning character's Pawn-channel collision/MovementMode/
	// camera collision test, launches it back/up off the monster, and calls MountedMonster->OnDismounted().
	// No-op if not currently mounted.
	UFUNCTION(BlueprintCallable, Category = "Mounting")
	void Dismount();

	// Called by ARiderCharacter::OnKnockedOffMount() (itself called from ABaseCharacter::TakeDamage()
	// whenever ANY damage is taken while mounted) — dismounts and launches the rider away from
	// DamageCauser. No-op if not currently mounted.
	void HandleKnockedOffMount(AActor* DamageCauser);

protected:
	virtual void BeginPlay() override;

private:
	// MountedMonster's own GetMesh()/GetCapsuleComponent() response to ECC_Camera as of just
	// before TryMount() overrode it to Ignore for this mount — restored exactly on Dismount()
	// rather than assuming ECR_Block, in case a monster BP customized it. Camera collision
	// itself (CameraBoom->bDoCollisionTest) now stays ALWAYS true — only this specific
	// monster's camera-channel response is touched, so the boom still collides normally with
	// floors/walls/everything else while mounted.
	ECollisionResponse PreMountMonsterMeshCameraResponse = ECR_Block;
	ECollisionResponse PreMountMonsterCapsuleCameraResponse = ECR_Block;

	// The owning character, cached in BeginPlay() — every actor-level mount effect (attachment,
	// collision, camera, equipped items) is applied to this, since a UActorComponent has no transform of
	// its own. Weak-equivalent via TWeakObjectPtr isn't needed here (a component never outlives its
	// owner), but IsValid() is still checked at every use site defensively.
	UPROPERTY()
	TObjectPtr<ABaseCharacter> OwnerCharacter;
};

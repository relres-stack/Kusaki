// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "RiderHookComponent.generated.h"

class AHookPoint;
class ARiderCharacter;

// The Rider's "hook grapple" — replaces the earlier "jump between monsters" idea. Rider-EXCLUSIVE (created
// only in ARiderCharacter's constructor, same precedent as ARiderCharacter::OilReserve), unlike the
// universal UHidingComponent/UProbeComponent that every hero gets. Only reachable while mounted: aiming
// at a designer-placed AHookPoint and pressing F (see ARiderCharacter::OnMountOrHookPressed()'s context-
// sensitive dispatch) tears the Rider off the monster (UMountRiderComponent::Dismount() — the monster is
// left fully free) and hangs her on the hook instead. While hanging, she's invisible to ground monsters by
// REUSING the existing UHidingComponent::bIsHidden/IsHiddenInSpot() path (SetHiddenExternally() below) —
// ABaseMonster::IsPlayerVisible()/IsPlayerLookingAtMonster() already OR-check IsHiddenInSpot() from the
// hiding-spot task, so nothing in ABaseMonster needs touching again. Detection uses the same aim/crosshair
// trace mounting already uses (ABaseCharacter::TraceAimTarget()) rather than a separate scan — see
// AHookPoint's own header comment for the collision-channel side of that.
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class MYCHARACTERS_API URiderHookComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	URiderHookComponent();

	// Max distance (in units) between the Rider and an aimed-at AHookPoint that TryGrapple() allows a
	// grapple at — same role as UMountRiderComponent::MountDistanceThreshold for mounting.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rider Hook")
	float HookGrappleRange = 800.0f;

	// True while hanging on CurrentHook.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rider Hook")
	bool bIsHanging = false;

	// The AHookPoint currently occupied, or nullptr if not hanging. Weak since this component doesn't own
	// the hook's lifetime.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rider Hook")
	TWeakObjectPtr<AHookPoint> CurrentHook;

	bool IsHanging() const { return bIsHanging; }

	// No-ops if Hook is invalid or farther than HookGrappleRange. Otherwise: dismounts the owner's
	// currently-ridden monster (a clean dismount, same as a voluntary Space dismount — the monster is left
	// fully free), teleports the owner to Hook's location, disables movement/collision (same "can't be
	// physically touched from below" reasoning as UHidingComponent::EnterHidingSpot()), and marks the owner
	// hidden via UHidingComponent::SetHiddenExternally(true). Called from
	// ARiderCharacter::OnMountOrHookPressed() when mounted and aiming at a hook.
	UFUNCTION(BlueprintCallable, Category = "Rider Hook")
	void TryGrapple(AHookPoint* Hook);

	// Re-enables collision, switches movement to MOVE_Falling (drops, doesn't just stand), and clears the
	// UHidingComponent::SetHiddenExternally(false) flag — ordinary detection resumes immediately. Called
	// from ARiderCharacter::OnMountOrHookPressed() when F is pressed again while hanging.
	UFUNCTION(BlueprintCallable, Category = "Rider Hook")
	void DropFromHook();

protected:
	virtual void BeginPlay() override;

private:
	// The owning Rider, cached in BeginPlay() — every actor-level effect (position, collision, movement)
	// is applied to this, since a UActorComponent has no transform of its own. Same caching pattern as
	// UMountRiderComponent::OwnerCharacter.
	UPROPERTY()
	TObjectPtr<ARiderCharacter> OwnerCharacter;
};

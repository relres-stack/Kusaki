// Fill out your copyright notice in the Description page of Project Settings.

#include "MountRiderComponent.h"
#include "BaseCharacter.h"
#include "BaseMonster.h"
#include "HolyItem.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"

UMountRiderComponent::UMountRiderComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UMountRiderComponent::BeginPlay()
{
	Super::BeginPlay();

	OwnerCharacter = Cast<ABaseCharacter>(GetOwner());
	if (!IsValid(OwnerCharacter))
	{
		UE_LOG(LogTemp, Error, TEXT("%s: UMountRiderComponent attached to a non-ABaseCharacter owner (%s) — mounting will never work"),
			*GetName(), IsValid(GetOwner()) ? *GetOwner()->GetName() : TEXT("None"));
	}
}

bool UMountRiderComponent::CanMountMonster(const ABaseMonster* Monster, float DistanceToMonster, FString& OutReason) const
{
	if (!IsValid(Monster))
	{
		OutReason = TEXT("Target Monster == None");
		return false;
	}

	// Every failing condition is collected, not just the first — a single [Mount Attempt] readout should
	// tell the whole story (e.g. a dead, already-ridden monster reports BOTH reasons at once) instead of
	// needing several presses to discover each blocker in turn as earlier ones get fixed.
	TArray<FString> Reasons;

	// Owner-side incapacitation checks — an owner in Panic (knocked down, mid-breathing-minigame) or
	// caught in a Bear Trap shouldn't be able to mount at all: manual test found mounting mid-panic left
	// the panic minigame running while seated on the monster, and mounting while trapped left the trap
	// attached but swapped the owner's movement speed for the monster's. IsValid(OwnerCharacter)
	// guaranteed by TryMount()'s own guard before this is ever called.
	if (OwnerCharacter->bIsPanicking)
	{
		Reasons.Add(TEXT("Owner bIsPanicking == true"));
	}
	if (OwnerCharacter->bIsTrapped)
	{
		Reasons.Add(TEXT("Owner bIsTrapped == true (in Bear Trap)"));
	}

	if (Monster->IsPendingKillPending())
	{
		Reasons.Add(TEXT("pending kill"));
	}
	if (Monster->Health <= 0.0f)
	{
		Reasons.Add(FString::Printf(TEXT("bIsDead (Health %.1f <= 0)"), Monster->Health));
	}
	if (!Monster->bCanBeMounted)
	{
		Reasons.Add(TEXT("bCanBeMounted == false"));
	}
	if (Monster->bIsBeingRidden)
	{
		Reasons.Add(TEXT("bIsBeingRidden == true (already mounted)"));
	}
	if (!IsValid(Monster->GetMesh()))
	{
		Reasons.Add(TEXT("GetMesh() == None"));
	}
	if (DistanceToMonster > MountDistanceThreshold)
	{
		Reasons.Add(FString::Printf(TEXT("Distance (%.1f > %.1f)"), DistanceToMonster, MountDistanceThreshold));
	}

	if (Reasons.Num() == 0)
	{
		OutReason = TEXT("OK");
		return true;
	}

	OutReason = FString::Join(Reasons, TEXT(", "));
	return false;
}

void UMountRiderComponent::TryMount()
{
	if (bIsMounted || !IsValid(OwnerCharacter))
	{
		return;
	}

	// Unified with the crosshair/aim system — mounts whatever the SAME trace that colors the HUD dot
	// green is currently aimed at (see this class's own header comment for why), rather than running an
	// independent forward sweep.
	FVector TargetPoint;
	FHitResult AimHit;
	OwnerCharacter->TraceAimTarget(TargetPoint, AimHit);

	ABaseMonster* TargetMonster = Cast<ABaseMonster>(AimHit.GetActor());
	const float DistanceToTarget = IsValid(TargetMonster) ? FVector::Dist(OwnerCharacter->GetActorLocation(), TargetMonster->GetActorLocation()) : -1.0f;

	FString CanMountReason;
	const bool bCanMount = CanMountMonster(TargetMonster, DistanceToTarget, CanMountReason);

	const FString TargetNameForDiagnostic = IsValid(TargetMonster) ? TargetMonster->GetName() : TEXT("None");
	const FString DistanceForDiagnostic = IsValid(TargetMonster) ? FString::Printf(TEXT("%.1f"), DistanceToTarget) : TEXT("N/A");

	UE_LOG(LogTemp, Warning, TEXT("[Mount Attempt] Player pressed F | Target Monster: %s | CanMount: %s | Distance: %s | Reason: %s"),
		*TargetNameForDiagnostic, bCanMount ? TEXT("YES") : TEXT("NO"), *DistanceForDiagnostic, *CanMountReason);
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(227, 3.0f, bCanMount ? FColor::Green : FColor::Red,
			FString::Printf(TEXT("[Mount Attempt] Target: %s | CanMount: %s | Dist: %s | %s"),
				*TargetNameForDiagnostic, bCanMount ? TEXT("YES") : TEXT("NO"), *DistanceForDiagnostic, *CanMountReason));
	}

	if (!bCanMount)
	{
		return;
	}

	bIsMounted = true;
	MountedMonster = TargetMonster;

	// Fixes the actual cause of the "jump" on mount: walking up close enough to the monster to be in
	// mounting range means the player's own capsule is already touching/overlapping the monster's Pawn-
	// blocking capsule, and Unreal's physics solver resolves that interpenetration with a push — which
	// can come out mostly vertical depending on capsule geometry, reading exactly like an unwanted jump.
	// Ignoring the Pawn channel specifically (not a blanket SetActorEnableCollision(false)) stops the
	// rider's capsule from fighting the monster's capsule/mesh at all, and MOVE_None stops
	// CharacterMovementComponent from doing any further movement/collision work afterward — both applied
	// BEFORE attaching, so nothing has a chance to push the player between now and the attach below.
	if (UCapsuleComponent* PlayerCapsule = OwnerCharacter->GetCapsuleComponent())
	{
		PlayerCapsule->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	}
	if (UCharacterMovementComponent* MoveComp = OwnerCharacter->GetCharacterMovement())
	{
		MoveComp->SetMovementMode(MOVE_None);
	}

	// Whatever item(s) the player is holding stay equipped/active while mounted (their light/aura keeps
	// working), but their collision/physics are forced fully off for the whole mount: a held item's mesh
	// (and, for a Blueprint subclass like BP_Censer, any additional physics bodies it might add, e.g. a
	// swinging-chain physics asset PickUp() has no knowledge of) rides the hand socket right next to the
	// monster's own mesh while riding it, and could otherwise physically interpenetrate/depenetrate
	// against it every tick. Restored to the normal "held" state (not re-enabled outright) by Dismount().
	if (IsValid(OwnerCharacter->EquippedRightHand))
	{
		OwnerCharacter->EquippedRightHand->DisableAllPhysicsAndCollision();
	}
	if (IsValid(OwnerCharacter->EquippedLeftHand))
	{
		OwnerCharacter->EquippedLeftHand->DisableAllPhysicsAndCollision();
	}

	// Attaches to GetMesh() itself, deliberately WITHOUT a socket name — a specific bone socket (e.g.
	// "Spine_03") isn't safe to apply a flat Z offset relative to: an individual bone's local axes can be
	// tilted arbitrarily relative to world up, which is exactly what put the rider sideways on the
	// ground in an earlier version of this system. The mesh component's own root doesn't have that
	// problem.
	OwnerCharacter->AttachToComponent(TargetMonster->GetMesh(), FAttachmentTransformRules::SnapToTargetNotIncludingScale);

	// Computed from THIS monster's own capsule height (rather than a single flat constant) so the rider
	// sits correctly above monsters of different sizes. Falls back to MountFallbackFixedHeightOffset only
	// if GetCapsuleComponent() is somehow invalid (shouldn't happen for a normal ACharacter).
	const UCapsuleComponent* MonsterCapsule = TargetMonster->GetCapsuleComponent();
	const float HeightOffset = IsValid(MonsterCapsule)
		? (MonsterCapsule->GetScaledCapsuleHalfHeight() + MountRiderHeightBuffer)
		: MountFallbackFixedHeightOffset;
	const FVector MountRelativeLocation(0.0f, 0.0f, HeightOffset);

	// A flat ZERO relative rotation here is exactly what previously left the rider facing 90 degrees
	// sideways: since we're attached to GetMesh() (not the capsule), zero relative rotation means the
	// rider's WORLD rotation equals the MESH's world rotation — and a standard humanoid skeletal mesh's
	// RelativeRotation to its own capsule is (0, -90, 0) yaw (the usual mesh-to-capsule offset), so the
	// rider ends up rotated 90 degrees off from the monster's actual facing/movement direction. The fix:
	// use the INVERSE of the mesh's own relative rotation, so composing it with the mesh's world rotation
	// cancels that offset back out to exactly the monster's actor (capsule) rotation — the direction
	// CharacterMovement/AI actually treats as "forward" for this monster.
	const FRotator MountRelativeRotation = TargetMonster->GetMesh()->GetRelativeRotation().Quaternion().Inverse().Rotator();
	OwnerCharacter->SetActorRelativeLocation(MountRelativeLocation);
	OwnerCharacter->SetActorRelativeRotation(MountRelativeRotation);

	UE_LOG(LogTemp, Warning, TEXT("TryMount: relative location set to %s (capsule half-height + buffer = %.1f), relative rotation set to %s (mesh's own relative rotation was %s)"),
		*MountRelativeLocation.ToCompactString(), HeightOffset, *MountRelativeRotation.ToCompactString(), *TargetMonster->GetMesh()->GetRelativeRotation().ToCompactString());

	// CameraBoom->bDoCollisionTest stays true for the whole mount now — instead of disabling
	// camera collision probing entirely, ignore ONLY this specific monster on the Camera
	// channel, so the boom keeps colliding normally with floors/walls/everything else while
	// mounted (previously the boom clipped through all geometry while mounted, not just the
	// monster — this surfaced as the camera diving through the floor when aiming steeply
	// upward, e.g. at a ceiling AHookPoint).
	if (UPrimitiveComponent* MonsterMesh = TargetMonster->GetMesh())
	{
		PreMountMonsterMeshCameraResponse = MonsterMesh->GetCollisionResponseToChannel(ECC_Camera);
		MonsterMesh->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	}
	if (UCapsuleComponent* MonsterCapsuleForCamera = TargetMonster->GetCapsuleComponent())
	{
		PreMountMonsterCapsuleCameraResponse = MonsterCapsuleForCamera->GetCollisionResponseToChannel(ECC_Camera);
		MonsterCapsuleForCamera->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	}

	TargetMonster->OnMounted(OwnerCharacter);

	UE_LOG(LogTemp, Warning, TEXT("Successfully mounted %s!"), *TargetMonster->GetName());
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(1003, 3.0f, FColor::Green,
			FString::Printf(TEXT("Successfully mounted %s!"), *TargetMonster->GetName()));
	}
}

void UMountRiderComponent::Dismount()
{
	if (!bIsMounted || !IsValid(OwnerCharacter))
	{
		return;
	}

	OwnerCharacter->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);

	// Restores exactly what TryMount() changed, in the same targeted way.
	if (UCapsuleComponent* PlayerCapsule = OwnerCharacter->GetCapsuleComponent())
	{
		PlayerCapsule->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
	}
	if (UCharacterMovementComponent* MoveComp = OwnerCharacter->GetCharacterMovement())
	{
		// MOVE_Falling, not MOVE_Walking: the rider is airborne from the LaunchCharacter() below
		// regardless, so CharacterMovement needs to re-acquire the ground itself (which MOVE_Falling
		// naturally resolves into MOVE_Walking the instant it lands) rather than being told it's already
		// walking on a floor that isn't under it anymore.
		MoveComp->SetMovementMode(MOVE_Falling);
	}

	// Restores whatever's still equipped back to the normal "held" collision/physics state — NOT fully
	// re-enabled, since a held item should never have collision/physics active regardless of mount state
	// (see TryMount()'s comment and RestoreHeldPhysicsState() itself).
	if (IsValid(OwnerCharacter->EquippedRightHand))
	{
		OwnerCharacter->EquippedRightHand->RestoreHeldPhysicsState();
	}
	if (IsValid(OwnerCharacter->EquippedLeftHand))
	{
		OwnerCharacter->EquippedLeftHand->RestoreHeldPhysicsState();
	}

	// Restore MountedMonster's own camera-channel response exactly to what it was before this
	// mount touched it (see TryMount()) — CameraBoom->bDoCollisionTest itself was never
	// changed, so nothing to restore there anymore.
	if (IsValid(MountedMonster))
	{
		if (UPrimitiveComponent* MonsterMesh = MountedMonster->GetMesh())
		{
			MonsterMesh->SetCollisionResponseToChannel(ECC_Camera, PreMountMonsterMeshCameraResponse);
		}
		if (UCapsuleComponent* MonsterCapsuleForCamera = MountedMonster->GetCapsuleComponent())
		{
			MonsterCapsuleForCamera->SetCollisionResponseToChannel(ECC_Camera, PreMountMonsterCapsuleCameraResponse);
		}
	}

	// Clears the ground/monster the player was just standing on so the launch is actually visible instead
	// of being immediately reabsorbed by standing collision the instant it's restored above.
	OwnerCharacter->LaunchCharacter(-OwnerCharacter->GetActorForwardVector() * 300.0f + FVector(0.0f, 0.0f, 200.0f), true, true);

	if (IsValid(MountedMonster))
	{
		MountedMonster->OnDismounted();
	}

	UE_LOG(LogTemp, Warning, TEXT("Dismounted from monster."));
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(1004, 3.0f, FColor::Yellow, TEXT("Dismounted from monster."));
	}

	bIsMounted = false;
	MountedMonster = nullptr;
}

void UMountRiderComponent::HandleKnockedOffMount(AActor* DamageCauser)
{
	if (!bIsMounted || !IsValid(OwnerCharacter))
	{
		return;
	}

	const FVector KnockDirection = IsValid(DamageCauser)
		? (OwnerCharacter->GetActorLocation() - DamageCauser->GetActorLocation()).GetSafeNormal2D()
		: -OwnerCharacter->GetActorForwardVector();

	// Dismount() itself already launches the rider backward as part of an ordinary voluntary dismount —
	// that's harmless to run here too (LaunchCharacter() fully overrides velocity, so the stronger
	// attacker-relative launch right after it simply wins, same frame), rather than duplicating
	// Dismount()'s detach/collision-restore/OnDismounted()/logging logic separately here.
	Dismount();
	OwnerCharacter->LaunchCharacter(KnockDirection * MountKnockOffForce + FVector(0.0f, 0.0f, MountKnockOffUpwardForce), true, true);

	UE_LOG(LogTemp, Warning, TEXT("Knocked off mount by %s!"), IsValid(DamageCauser) ? *DamageCauser->GetName() : TEXT("unknown"));
}

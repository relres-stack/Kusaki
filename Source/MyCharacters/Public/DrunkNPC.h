// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DrunkNPC.generated.h"

class UStaticMeshComponent;
class ADrunkNPCSpawner;
class ABaseMonster;
class UMoonshineComponent;

// "Спаивание НПС-собутыльников" (Roadmap Фаза 3, п.4) — a decoy the Redneck feeds Moonshine to (see
// TryGetDrunk(), called from ARedneckCharacter::Interact()), carries on his back, then throws at a
// point he's aiming at (ThrowTo(), called from ARedneckCharacter::TryHijackAttackPress()). Where it
// lands, it lures nearby monsters in a radius and holds their attention (freezing them via
// DisableMovement()) for a duration — both derived from how much Moonshine it actually drank, which is
// itself random (see UMoonshineComponent::TryConsumeForNPC() — a SEPARATE, already-landed task: this
// class only calls that function, never edits UMoonshineComponent). No animation for this first
// version — an idle NPC, a carried NPC, and monsters "eating" it are all just state, not poses; visual
// polish is explicitly deferred. Depends on UMoonshineComponent::TryConsumeForNPC() already existing —
// see that task's own notes; this one only calls it.
UENUM(BlueprintType)
enum class EDrunkNPCState : uint8
{
	Idle,
	Carried,
	Luring,
	Consumed
};

UCLASS()
class MYCHARACTERS_API ADrunkNPC : public AActor
{
	GENERATED_BODY()

public:
	ADrunkNPC();

	// Optional placeholder visual — no meaningful animation for this version (idle/carried/luring are
	// all just EDrunkNPCState, not poses), so a designer can assign any humanoid-ish static mesh on a
	// BP subclass purely for silhouette purposes.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Drunk NPC")
	TObjectPtr<UStaticMeshComponent> NPCMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Drunk NPC")
	EDrunkNPCState State = EDrunkNPCState::Idle;

	// Set by ADrunkNPCSpawner::SpawnNPC() right after spawning — not a UPROPERTY (code-assigned, not
	// designer-assigned), same reasoning as similar spawner-owner back-references elsewhere in this
	// project.
	TWeakObjectPtr<ADrunkNPCSpawner> OwningSpawner;

	// Range of how much of the shared Moonshine stock this NPC drinks on TryGetDrunk() — a "generous
	// sip" (may drain almost the whole remaining bottle, per the user's own wording).
	UPROPERTY(EditAnywhere, Category = "Lure")
	float MinSipAmount = 5.0f;

	UPROPERTY(EditAnywhere, Category = "Lure")
	float MaxSipAmount = 40.0f;

	// ResolvedLureRadius = BaseLureRadius + AmountConsumed * LureRadiusPerUnitConsumed — see
	// TryGetDrunk().
	UPROPERTY(EditAnywhere, Category = "Lure")
	float BaseLureRadius = 400.0f;

	UPROPERTY(EditAnywhere, Category = "Lure")
	float LureRadiusPerUnitConsumed = 15.0f;

	// ResolvedSurviveDuration = BaseSurviveDuration + AmountConsumed * SurviveDurationPerUnitConsumed —
	// see TryGetDrunk(). The clock only starts once the FIRST monster actually arrives (see Tick()),
	// not from the moment of the throw.
	UPROPERTY(EditAnywhere, Category = "Lure")
	float BaseSurviveDuration = 3.0f;

	UPROPERTY(EditAnywhere, Category = "Lure")
	float SurviveDurationPerUnitConsumed = 0.3f;

	// How close a monster must get to this NPC to count as "started eating" (see Tick()).
	UPROPERTY(EditAnywhere, Category = "Lure")
	float EatProximityRadius = 150.0f;

	// Bone/socket on the carrying character's mesh this NPC snaps to in TryGetDrunk() — a placeholder
	// name, not necessarily correct for how this should actually look; adjust after the first PIE test.
	UPROPERTY(EditAnywhere, Category = "Lure")
	FName CarrySocketName = TEXT("spine_03");

	// Computed once in TryGetDrunk(), consumed by ThrowTo()/Tick()/OnSurviveTimeExpired().
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Lure")
	float ResolvedLureRadius = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Lure")
	float ResolvedSurviveDuration = 0.0f;

	// Monsters currently frozen at this NPC, eating it — restored to normal movement in
	// OnSurviveTimeExpired(). Not a UPROPERTY — see OwningSpawner's own comment on why (code-managed
	// runtime state, and a TWeakObjectPtr array doesn't need reflection to stay safe against the
	// referenced monster being destroyed).
	TArray<TWeakObjectPtr<ABaseMonster>> MonstersCurrentlyEating;

	// Only if State == Idle: spends FromComponent->TryConsumeForNPC() (a random sip out of the shared
	// Moonshine stock), derives ResolvedLureRadius/ResolvedSurviveDuration from however much was
	// actually consumed, switches to Carried, and disables collision (so it doesn't block the Redneck's
	// own movement while attached to him). Returns false (no state change) if the stock is already
	// empty — nothing to get drunk on. Called from ARedneckCharacter::Interact().
	UFUNCTION(BlueprintCallable, Category = "Lure")
	bool TryGetDrunk(UMoonshineComponent* FromComponent);

	// Only if State == Carried: detaches, re-enables collision, teleports to TargetPoint (a straight
	// teleport, no throw arc/ballistics — simpler and more reliable for this first version), switches
	// to Luring, and starts the lure (StartLuring()). Called from
	// ARedneckCharacter::TryHijackAttackPress().
	UFUNCTION(BlueprintCallable, Category = "Lure")
	void ThrowTo(const FVector& TargetPoint);

protected:
	virtual void Tick(float DeltaTime) override;

private:
	// One-shot on landing (see ThrowTo()): sweeps ResolvedLureRadius for every living ABaseMonster and
	// sends each one moving to this NPC's location via its own public RequestMoveToLocation() — same
	// "call the monster's own public move-request choke point" precedent as
	// UMountRiderComponent/ABearTrap::ReleaseRiddenMonster() already use, ABaseMonster itself untouched.
	void StartLuring();

	// Timer callback for SurviveTimerHandle, started the moment the FIRST monster actually arrives (see
	// Tick()) — restores every entry in MonstersCurrentlyEating to normal movement, notifies
	// OwningSpawner, and destroys this actor.
	void OnSurviveTimeExpired();

	FTimerHandle SurviveTimerHandle;
};

// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BearTrap.generated.h"

class UStaticMeshComponent;
class USphereComponent;
class ABaseCharacter;
class ABaseMonster;

// Act 1.3 ("Реднек и Капкан") environmental hazard: a placeable trap that snaps onto (and drags along
// with) whichever ABaseCharacter walks into it, forcing a mash-to-escape QTE — see ABaseCharacter's own
// "Bear Trap" section (bIsTrapped/CaughtInTrap()/MashTrapEscape()) for the escape side of the mechanic;
// this class only owns detection + attaching itself + detaching once freed. A plain UCLASS() AActor
// (Blueprintable + Placeable by default), not Abstract, so it can be dragged straight into a level with
// zero Blueprint setup required — TrapMesh already has a working placeholder visual (see the constructor)
// a designer can override in a BP_BearTrap child if they want a real trap mesh instead.
UCLASS()
class MYCHARACTERS_API ABearTrap : public AActor
{
	GENERATED_BODY()

public:
	ABearTrap();

	// Placeholder visual (a flattened Cylinder, same "working default before a real asset exists"
	// precedent as AHolyItem's Candle placeholder) — replace the static mesh on a BP_BearTrap child for a
	// real trap model; the collision/attach logic below doesn't care what's actually assigned here.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Bear Trap")
	TObjectPtr<UStaticMeshComponent> TrapMesh;

	// Overlap-only trigger — catches whichever ABaseCharacter walks into it. Kept separate from TrapMesh's
	// own (blocking, so the trap visually sits solidly on the ground) collision so the two never interfere.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Bear Trap")
	TObjectPtr<USphereComponent> TriggerCollision;

	// Socket/bone this trap snaps to on the caught character's mesh — right foot tried first, left as a
	// fallback, same "try primary, fall back" resolution AHolyItem's hand-socket attachment already uses.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bear Trap")
	FName RightFootSocket = TEXT("foot_r");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bear Trap")
	FName LeftFootSocket = TEXT("foot_l");

	// Called by ABaseCharacter::EscapeTrap() once its QTE completes — detaches from the (now-freed)
	// character and destroys itself; a sprung trap is spent, not reusable/re-catchable.
	void OnPlayerEscaped();

	// How long (seconds) a ridden ABaseMonster caught in this trap is held in place (MOVE_None) before
	// being released and its rider dismounted — see OnTriggerOverlap()'s monster branch/ReleaseRiddenMonster().
	// Placeholder value, balance later.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bear Trap")
	float RiddenMonsterHoldDuration = 3.0f;

protected:
	virtual void BeginPlay() override;

private:
	UFUNCTION()
	void OnTriggerOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	// True once this trap has caught someone — guards against OnTriggerOverlap() re-firing (e.g. another
	// actor grazing the trigger sphere, or the same character re-overlapping mid-QTE as it's dragged
	// around) after this trap is already attached and mid-escape.
	bool bHasCaughtSomeone = false;

	// Fires RiddenMonsterHoldDuration seconds after a ridden ABaseMonster steps into this trap (see
	// OnTriggerOverlap()'s monster branch) — restores the monster's movement, dismounts its rider via
	// UMountRiderComponent::Dismount(), then destroys this trap (a sprung trap is spent, same as
	// OnPlayerEscaped()).
	void ReleaseRiddenMonster(TWeakObjectPtr<ABaseMonster> MonsterPtr);

	// Timer handle for ReleaseRiddenMonster() above.
	FTimerHandle RiddenMonsterReleaseTimerHandle;
};

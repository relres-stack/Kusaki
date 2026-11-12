// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BinocularsPickup.generated.h"

class UStaticMeshComponent;
class UTextRenderComponent;

// The hilltop binoculars beat: Redneck walks up, sees the "Press E" prompt, looks through the binoculars
// (a one-shot, no re-look) to see a designer-authored step count for the road ahead, then a set of
// pre-placed (currently hidden) phantom trail markers on the level light up. The binoculars themselves
// visually "put away" (hidden) afterward — a placeholder stand-in for a real holster/belt-attach animation
// later. Deliberately NOT the real Hunter's Focus step-count formula (UHunterFocusComponent) — this is a
// scripted, single-use level moment with a number the designer sets per-level, not a live mechanic;
// DisplayedStepCount is just data. A plain AActor (AHolyItem/ABearTrap's own precedent), not a component —
// this is a world prop/pickup, not a character ability. What happens after this moment ("разбитие" —
// a future task) is out of scope here entirely; this class's job ends the instant the trail markers are
// revealed.
UCLASS()
class MYCHARACTERS_API ABinocularsPickup : public AActor
{
	GENERATED_BODY()

public:
	ABinocularsPickup();

	// Placeholder visual — a designer can override the static mesh on a BP child, same "working default
	// before a real asset exists" precedent as ABearTrap::TrapMesh/AHolyItem's Candle placeholder.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Binoculars")
	TObjectPtr<UStaticMeshComponent> BinocularsMesh;

	// "Press E" prompt, visible above the actor until used — a plain UTextRenderComponent placeholder, no
	// dedicated UI widget for this one-off moment.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Binoculars")
	TObjectPtr<UTextRenderComponent> PromptText;

	// Hardcoded step count shown on use — a designer-authored number for this specific level's road
	// distance, not a computed value. Deliberately NOT the Hunter's Focus formula — see this class's own
	// header comment.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Binoculars")
	int32 DisplayedStepCount = 12;

	// How close (world units) ARedneckCharacter::Interact() must be to trigger LookThroughBinoculars()
	// instead of falling through to its ordinary pickup/drop behavior.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Binoculars")
	float InteractRadius = 300.0f;

	// Pre-placed, designer-hidden (SetActorHiddenInGame(true) by default in the level) phantom trail
	// markers on the road — revealed (SetActorHiddenInGame(false)) the moment LookThroughBinoculars()
	// succeeds. Not this class's own actors — just references into whatever the level already has placed.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Binoculars")
	TArray<AActor*> PhantomTrailMarkers;

	// True once LookThroughBinoculars() has run — a one-shot moment, never re-triggerable.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Binoculars")
	bool bHasBeenUsed = false;

	// The actual one-shot look: no-ops if bHasBeenUsed already. Otherwise sets bHasBeenUsed, hides
	// PromptText, shows DisplayedStepCount via AddOnScreenDebugMessage() (same screen-text placeholder
	// approach already used for Прощупывание/Огонёк на пальцах — no dedicated UI widget for this yet),
	// reveals every entry in PhantomTrailMarkers, and hides this actor itself (the binoculars "put away").
	// Called from ARedneckCharacter::Interact() — see that override's own comment.
	UFUNCTION(BlueprintCallable, Category = "Binoculars")
	void LookThroughBinoculars();

protected:
	virtual void BeginPlay() override;
};

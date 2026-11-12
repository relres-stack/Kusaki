// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HidingSpot.generated.h"

class UStaticMeshComponent;
class UTextRenderComponent;

// A world prop (wardrobe/crate/barrel) the player can hide inside via UHidingComponent — see that
// component's own header comment for the full "Укрытия" mechanic (Roadmap Фаза 2, п.3). A plain AActor,
// not a component — this is a world object, same "prop, not ability" reasoning as ABinocularsPickup/
// ABearTrap. Designer assigns the actual mesh (wardrobe/crate/barrel) on SpotMesh in a BP subclass — no
// placeholder primitive here, unlike ABinocularsPickup's cylinder stand-in, since the exact shape matters
// for where the player visually ends up standing inside it.
UCLASS()
class MYCHARACTERS_API AHidingSpot : public AActor
{
	GENERATED_BODY()

public:
	AHidingSpot();

	// Designer-assigned visual — wardrobe/crate/barrel mesh, set on a BP subclass.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hiding Spot")
	TObjectPtr<UStaticMeshComponent> SpotMesh;

	// "Press E" prompt, visible only while a player character is within PromptDisplayRadius AND this spot
	// isn't already occupied — checked every Tick() rather than an overlap volume, same "no extra
	// component" reasoning as UHidingComponent::TryToggle()'s own distance-based search.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hiding Spot")
	TObjectPtr<UTextRenderComponent> PromptText;

	// How close (world units) the player must be for PromptText to show.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hiding Spot")
	float PromptDisplayRadius = 200.0f;

	// True while a hero is currently hidden here — see SetOccupied()/IsOccupied().
	bool IsOccupied() const { return bOccupied; }

	// Set by UHidingComponent::EnterHidingSpot()/ExitHidingSpot() as the owner enters/leaves this spot.
	void SetOccupied(bool bNewOccupied) { bOccupied = bNewOccupied; }

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

private:
	// True while a hero is hidden here — see SetOccupied()/IsOccupied() above. Kept private:
	// UHidingComponent (not a subclass of this class) reaches it only through those two accessors.
	bool bOccupied = false;
};

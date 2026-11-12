// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HookPoint.generated.h"

class UStaticMeshComponent;

// Designer-placed grapple point for URiderHookComponent — see that component's own header comment for the
// full "hook grapple" mechanic. Pure world prop, no runtime spawning, placed by hand on ledges/ceilings —
// same "plain AActor, no logic of its own" precedent as ABearTrap/ABinocularsPickup/AHidingSpot; all
// hanging/dropping/hidden-state behavior lives in URiderHookComponent, this class only needs to be
// something ABaseCharacter::TraceAimTarget() can hit.
UCLASS()
class MYCHARACTERS_API AHookPoint : public AActor
{
	GENERATED_BODY()

public:
	AHookPoint();

	// Placeholder visual — same "engine primitive stand-in before a real asset exists" precedent as
	// ABearTrap::TrapMesh/ABinocularsPickup::BinocularsMesh. Collision blocks ECC_Visibility — the same
	// channel ABaseCharacter::TraceAimTarget() traces on, so mounting's own aim/crosshair trace detects
	// this actor with no separate scan needed.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hook Point")
	TObjectPtr<UStaticMeshComponent> HookMesh;

	// Dynamic material instance created from HookMesh's material 0 in BeginPlay() — lets Tick()
	// pulse emissive color without touching the underlying asset.
	UPROPERTY()
	TObjectPtr<class UMaterialInstanceDynamic> HookDynamicMaterial;

	// Color the hook pulses toward when a mounted Rider is within jump range — see Tick().
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hook Point")
	FLinearColor HighlightColor = FLinearColor(0.1f, 0.9f, 1.0f, 1.0f);

	// Pulse speed (radians/sec fed into FMath::Sin) while a Rider is in range — purely cosmetic,
	// tune to taste.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hook Point")
	float HighlightPulseSpeed = 4.0f;

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;
};

// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "VisionFireSource.generated.h"

class UPointLightComponent;
class UNiagaraComponent;

// A placeable fire source (campfire/torch) for the "Воспалённое зрение" ("Inflamed Vision") ability.
// Shell only: this class just owns the light + fire VFX and knows how to switch itself on/off. Where it
// gets placed and when Activate()/Deactivate() actually get called (a trace finding a valid spot, etc.)
// is deliberately NOT here — that's a separate follow-up task. Plain AActor, not a component (same
// "world prop, not a character ability" reasoning as ABinocularsPickup/AHolyItem) — hidden and unlit by
// default so a level designer can drop it anywhere and it stays invisible until the ability turns it on.
UCLASS()
class MYCHARACTERS_API AVisionFireSource : public AActor
{
	GENERATED_BODY()

public:
	AVisionFireSource();

	// Warm campfire-like point light. Defaults: bright enough to read as fire (Intensity) but a short
	// AttenuationRadius so it lights a small area around itself rather than flooding an entire room.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vision Fire Source")
	TObjectPtr<UPointLightComponent> FireLight;

	// Fire VFX riding the same location as FireLight. No default Niagara System is assigned here — assign
	// one on the component (e.g. in a BP child) in the editor, same precedent as
	// AHolyItem::FlameParticleComponent.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vision Fire Source")
	TObjectPtr<UNiagaraComponent> FireNiagaraComponent;

	// Smoke VFX riding the same location as FireLight/FireNiagaraComponent (design doc Step 8, п.7 — actual
	// billowing "клубы", separate from AExponentialHeightFog's density gating in
	// the vision's screen look, which only affects overall scene haze, not visible
	// puffs/shapes). No default Niagara System assigned here — assign one on the component (e.g. in a BP
	// child), same precedent as FireNiagaraComponent above. Left unassigned is safe — an inactive Niagara
	// component with no system asset simply renders nothing.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vision Fire Source")
	TObjectPtr<UNiagaraComponent> SmokeNiagaraComponent;

	// Reveals the actor and turns FireLight, FireNiagaraComponent, and SmokeNiagaraComponent on.
	UFUNCTION(BlueprintCallable, Category = "Vision Fire Source")
	void Activate();

	// Hides the actor and turns FireLight, FireNiagaraComponent, and SmokeNiagaraComponent off.
	// DeactivateImmediate() (not just Deactivate()) on both Niagara components cuts already-emitted
	// particles right now instead of letting them finish their natural lifetime — same reasoning as
	// AHolyItem::Extinguish().
	UFUNCTION(BlueprintCallable, Category = "Vision Fire Source")
	void Deactivate();

protected:
	virtual void BeginPlay() override;
};

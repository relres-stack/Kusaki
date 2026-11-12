// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "FlashlightComponent.generated.h"

class USpotLightComponent;

// Ordinary light — vision only, NEVER repels monsters (see ABaseMonster::IsLocationProtected(), which
// only ever looks at EquippedRightHand/EquippedLeftHand, both AHolyItem — a flashlight is never equipped
// into either hand slot, just a light component riding the mesh, so it's structurally incapable of
// counting as holy protection). Shared by ARedneckCharacter and ASiblingCharacter — the ONLY two heroes
// with a flashlight — so battery drain/toggle logic exists in exactly one place.
//
// Does NOT create its own USpotLightComponent (a UActorComponent's constructor can't CreateDefaultSubobject
// a sibling component — only an owning AActor's constructor can): the owning character's constructor
// creates the actual USpotLightComponent itself, attaches it to the mesh, and assigns it to LightComponent
// here so this component's Toggle()/battery drain has something to act on.
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class MYCHARACTERS_API UFlashlightComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UFlashlightComponent();

	// The actual light, assigned by the owning character's constructor right after creating it. Toggle()
	// and the battery-drain Tick() both no-op harmlessly if this is never assigned.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flashlight")
	TObjectPtr<USpotLightComponent> LightComponent;

	// Remaining battery, [0, MaxBattery] — the Horror HUD's quick-slot bar reads this for whichever hero
	// has a flashlight equipped in the Light Source slot. Drains at BatteryDrainRate units/sec while the
	// light is actually on; auto-turns the light off at 0 rather than leaving a "0% battery, still lit"
	// inconsistency.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flashlight")
	float BatteryRemaining = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flashlight")
	float MaxBattery = 100.0f;

	// Battery units drained per second while the light is on. Default (100 battery / ~0.0278 per sec)
	// lasts about an hour of continuous use — slow enough not to matter for a short play session, but a
	// real, ticking-down value. Set to 0 for effectively infinite battery.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flashlight")
	float BatteryDrainRate = 0.0278f;

	// Toggles LightComponent's visibility on/off — refuses to turn on with 0 battery remaining.
	UFUNCTION(BlueprintCallable, Category = "Flashlight")
	void Toggle();

	// Defined in the .cpp, not inline here — USpotLightComponent is only forward-declared in this header,
	// and IsVisible() needs its complete type.
	UFUNCTION(BlueprintCallable, Category = "Flashlight")
	bool IsOn() const;

protected:
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
};

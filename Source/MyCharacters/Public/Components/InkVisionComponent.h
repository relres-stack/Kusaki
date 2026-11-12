// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "InkVisionComponent.generated.h"

class UMaterialInterface;
class UMaterialInstanceDynamic;

// "Чернильное зрение" (Rider): a fullscreen post-process material (M_InkVision — scene-depth outline +
// Stencil-based monster highlight + ink drops, already assembled in the editor) that this component turns
// on/off by driving its own runtime Dynamic Material Instance's "VisionEffectStrength" scalar, added to the
// level's Post Process Volume in BeginPlay() — same pattern as UVisionFireSourceComponent (Priest's
// "Воспалённое зрение"), just without that component's fire-source/fog machinery. Temporary + on cooldown,
// same shape as UVisionFireSourceComponent::ToggleVision()/VisionDuration/VisionCooldown. While active, the
// owner is treated as blind for monster-detection purposes (ABaseCharacter::SetBlindedByAbility()) without
// going through CloseEyes()/OpenEyes() (no nose-breathing minigame, no instant-kill-on-open side effects),
// and the eyes-closed darkness post-process is suppressed (SetEyesClosedDarknessSuppressed()) so it doesn't
// crush this ability's own screen effect — both toggled together, synchronously, in ToggleVision()/
// EndVisionEffect().
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class MYCHARACTERS_API UInkVisionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UInkVisionComponent();

	// The M_InkVision Material (or a Material Instance of it) — wrapped in this component's own runtime
	// Dynamic Material Instance in BeginPlay() and added to the level's (single, Unbound) Post Process
	// Volume, same pattern as UVisionFireSourceComponent::VisionPostProcessMaterial. Left unset (None)
	// makes this whole feature a safe no-op.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ink Vision")
	TObjectPtr<UMaterialInterface> InkVisionMaterial;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ink Vision")
	TObjectPtr<UMaterialInstanceDynamic> InkVisionMID;

	// Seconds the ability stays active before automatically turning itself back off. Starting value, tune
	// in PIE.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ink Vision")
	float VisionDuration = 10.0f;

	// Seconds after the ability ends before it can be used again. Starting value, tune in PIE.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ink Vision")
	float VisionCooldown = 15.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ink Vision")
	bool bCanUseVision = true;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ink Vision")
	bool bIsVisionActive = false;

	// Turns the ability on if inactive and off cooldown; cuts it short (straight into cooldown) if already
	// active. Bound to V by ARiderCharacter::SetupPlayerInputComponent().
	UFUNCTION(BlueprintCallable, Category = "Ink Vision")
	void ToggleVision();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	// Shared teardown for both the natural VisionDuration timeout and a manual mid-ability ToggleVision()
	// press.
	void EndVisionEffect();
	void EndVisionCooldown();

	FTimerHandle VisionDurationTimerHandle;
	FTimerHandle VisionCooldownTimerHandle;
};

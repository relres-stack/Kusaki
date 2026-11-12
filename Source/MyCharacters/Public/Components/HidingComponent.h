// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "HidingComponent.generated.h"

class ABaseCharacter;
class AHidingSpot;

// "Укрытия" (Фаза 2, п.3) — универсальная механика, доступна КАЖДОМУ герою: создаётся в конструкторе
// ABaseCharacter, тот же прецедент, что уже у UInventoryComponent/UProbeComponent — "каждый герой получает
// это независимо от подкласса". Привязан к E (третий независимый слушатель этой клавиши помимо
// ABaseCharacter::Interact()/MashTrapEscape() — см. ABaseCharacter::OnHidingTogglePressed()'s own comment)
// в ABaseCharacter::SetupPlayerInputComponent().
//
// Первая версия, ПОЛНОСТЬЮ безопасная: спрятавшись в AHidingSpot, персонаж целиком заморожен (не
// двигается, коллизия капсулы выключена) и НИКАК не может быть обнаружен монстром, ни при каких
// обстоятельствах — ни через обычную видимость/взгляд (ABaseMonster::IsPlayerVisible()/
// IsPlayerLookingAtMonster(), см. их точечные правки), ни физическим контактом (коллизия выключена, так
// что OnCapsuleHit() не может сработать вообще). Риска обнаружения/принудительного "вскрытия" укрытия
// монстром в этой версии нет — это отдельная будущая задача. Полностью отдельное состояние, не
// переиспользует систему закрытых глаз (bIsEyesClosed не трогается вообще).
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class MYCHARACTERS_API UHidingComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UHidingComponent();

	// True while the owner is hidden in CurrentHidingSpot — fully frozen (see EnterHidingSpot()).
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hiding")
	bool bIsHidden = false;

	// The AHidingSpot currently occupied, or nullptr if not hidden. Weak since this component doesn't own
	// the spot's lifetime.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hiding")
	TWeakObjectPtr<AHidingSpot> CurrentHidingSpot;

	// How close (world units) the owner must be to an unoccupied AHidingSpot for TryToggle() to enter it.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hiding")
	float HidingSpotSearchRadius = 200.0f;

	// If already hidden, calls ExitHidingSpot(). Otherwise: no-ops while the owner is trapped, panicking,
	// awaiting get-up, or (for a hero that can mount at all) currently mounted — same set of
	// incapacitation reasons UMountRiderComponent::CanMountMonster() already checks, just applied to this
	// action instead. Then finds the nearest unoccupied AHidingSpot within HidingSpotSearchRadius and, if
	// one exists, enters it via EnterHidingSpot(). Bound to E via
	// ABaseCharacter::OnHidingTogglePressed().
	UFUNCTION(BlueprintCallable, Category = "Hiding")
	void TryToggle();

	// Marks Spot occupied, teleports the owner into it, disables its movement and capsule collision (the
	// collision disable also guarantees a monster can never physically touch the owner's capsule while
	// hidden, so it can't trigger a Naskok/Blind Encounter via OnCapsuleHit() either — those are gated on
	// physical contact, not IsPlayerVisible()), and sets bIsHidden/CurrentHidingSpot.
	UFUNCTION(BlueprintCallable, Category = "Hiding")
	void EnterHidingSpot(AHidingSpot* Spot);

	// Re-enables the owner's capsule collision and walking movement, frees CurrentHidingSpot (if still
	// valid) via SetOccupied(false), and clears bIsHidden/CurrentHidingSpot. Ordinary detection applies
	// again immediately — nothing else needs restoring.
	UFUNCTION(BlueprintCallable, Category = "Hiding")
	void ExitHidingSpot();

	bool IsHidden() const { return bIsHidden; }

	// Lets an unrelated mechanic (currently only URiderHookComponent, hanging on an AHookPoint) reuse this
	// same bIsHidden/IsHiddenInSpot() path that already makes ABaseMonster::IsPlayerVisible()/
	// IsPlayerLookingAtMonster() ignore the player, without going through a real AHidingSpot at all.
	// Doesn't touch CurrentHidingSpot — a caller using this is expected to manage its own occupied state.
	void SetHiddenExternally(bool bHidden) { bIsHidden = bHidden; }

private:
	// Owner's transform just before EnterHidingSpot() teleports it into the spot — ExitHidingSpot() moves
	// the owner back here. Purely transient storage, not exposed as UPROPERTY.
	FVector PreHideLocation = FVector::ZeroVector;
	FRotator PreHideRotation = FRotator::ZeroRotator;
};

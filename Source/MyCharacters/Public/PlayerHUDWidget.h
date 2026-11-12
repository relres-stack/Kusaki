// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "InventoryComponent.h"
#include "PlayerHUDWidget.generated.h"

class ABaseCharacter;

// Fully native (no Widget Blueprint asset required) Horror HUD — health bar (bottom-left), 4-slot
// quick-slot bar (bottom-center, active slot highlighted), and mechanic/status readouts (Redneck
// Moonshine charges/boost timer, Light Source fuel/battery — both pulled from the active
// UInventoryComponent slots). ABaseCharacter creates and owns exactly one instance (see its
// BeginPlay()), added full-screen beneath the crosshair widget.
//
// Drawn via NativePaint() using the same asset-free Slate primitives as UCrosshairWidget
// (FSlateRoundedBoxBrush for boxes, FCoreStyle's default font for text) — works with zero Blueprint
// setup. To skin the HUD in Blueprint instead (a WBP_HUD child of this class): set bUseNativeDrawing to
// false (skips all native drawing) and bind to the BlueprintImplementableEvents below, which fire
// regardless of that flag whenever the underlying data actually changes — build your own UMG Image/
// ProgressBar/TextBlock hierarchy in the Blueprint's Designer and update it from those events.
UCLASS()
class MYCHARACTERS_API UPlayerHUDWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// While true (default), NativePaint() draws the whole HUD itself — see this class's own header
	// comment for the Blueprint-skinning alternative.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HUD")
	bool bUseNativeDrawing = true;

	// Fired whenever the owning character's Health changes (mirrors ABaseCharacter::OnHealthChanged) —
	// fires regardless of bUseNativeDrawing.
	UFUNCTION(BlueprintImplementableEvent, Category = "HUD")
	void OnHealthChangedBP(float NewHealth, float MaxHealth);

	// Fired whenever the active quick-slot changes (mirrors UInventoryComponent::OnActiveSlotChanged).
	UFUNCTION(BlueprintImplementableEvent, Category = "HUD")
	void OnActiveSlotChangedBP(int32 NewIndex, int32 OldIndex);

	// Fired once per slot whenever the inventory updates (mirrors UInventoryComponent::OnInventoryUpdated,
	// expanded per-slot here since that delegate itself carries no per-slot data).
	UFUNCTION(BlueprintImplementableEvent, Category = "HUD")
	void OnQuickSlotUpdatedBP(int32 SlotIndex, const FQuickSlotData& SlotData);

	// Fired whenever the interaction prompt text changes (mirrors what UCrosshairWidget draws — exposed
	// here too since a Blueprint-skinned HUD may want to render its own prompt rather than relying on the
	// crosshair widget's native one).
	UFUNCTION(BlueprintImplementableEvent, Category = "HUD")
	void OnInteractionPromptChangedBP(const FText& PromptText, bool bVisible);

	// Layout tuning — bottom-left health bar.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HUD|Layout")
	FVector2D HealthBarSize = FVector2D(220.0f, 24.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HUD|Layout")
	FVector2D HealthBarMargin = FVector2D(30.0f, 30.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HUD|Layout")
	FLinearColor HealthBarBackgroundColor = FLinearColor(0.05f, 0.05f, 0.05f, 0.75f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HUD|Layout")
	FLinearColor HealthBarFillColor = FLinearColor(0.75f, 0.1f, 0.1f, 0.9f);

	// Color of the "75 / 100" health readout drawn centered over the bar (amber, legible on both the
	// dark background and the red fill; drawn with DrawShadowedText()'s black outline).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HUD|Layout")
	FLinearColor HealthBarTextColor = FLinearColor(1.0f, 0.8f, 0.15f, 1.0f);

	// Layout tuning — bottom-center quick-slot bar.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HUD|Layout")
	FVector2D QuickSlotSize = FVector2D(56.0f, 56.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HUD|Layout")
	float QuickSlotSpacing = 12.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HUD|Layout")
	float QuickSlotBottomMargin = 30.0f;

	// Dark, semi-transparent — never solid/bright: this is drawn UNDER the item name/slot number/charge
	// text (see PaintQuickSlot()), so it must stay low-contrast against white text regardless of what's
	// behind the HUD in the world.
	// Dark, semi-transparent — never solid/bright: this is drawn UNDER the item name/slot number/charge
	// text (see PaintQuickSlot()), so it must stay low-contrast against white text regardless of what's
	// behind the HUD in the world.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HUD|Layout")
	FLinearColor QuickSlotBackgroundColor = FLinearColor(0.05f, 0.05f, 0.05f, 0.6f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HUD|Layout")
	FLinearColor QuickSlotActiveBorderColor = FLinearColor(0.95f, 0.85f, 0.2f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HUD|Layout")
	FLinearColor QuickSlotFillColor = FLinearColor(0.2f, 0.6f, 0.9f, 0.9f);

	// Color of the item name/label text drawn in a populated slot (e.g. "Candle"/"Censer"/"Flashlight") —
	// see PaintQuickSlot(). Full white (not off-white) for maximum contrast against the dark slot
	// background — paired with DrawShadowedText()'s black shadow so it stays legible even if something
	// bright ever shows up behind the HUD.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HUD|Layout")
	FLinearColor QuickSlotItemNameColor = FLinearColor::White;

	// Color of the numeric countdown text (e.g. Moonshine's "4.2s") PaintQuickSlot() draws along a slot's
	// bottom edge whenever FQuickSlotData::SecondsRemaining is nonzero — see that field's own comment.
	// Warm amber, not white, so it reads distinctly as "a timer" against the item name text above it.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HUD|Layout")
	FLinearColor QuickSlotTimerTextColor = FLinearColor(1.0f, 0.75f, 0.2f, 1.0f);

	// Alpha multiplier applied to an Empty slot's background/border/slot-number so it reads as visually
	// "off" at a glance — distinct from a populated slot, not just missing its fill bar/label/charge text.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HUD|Layout", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float EmptySlotOpacityMultiplier = 0.4f;

	// Breathing Rhythm Indicator (see PaintBreathingIndicator()) — a shrinking ring, converging on a
	// fixed target ring exactly at each breath beat, drawn screen-center whenever OwnerCharacter-
	// >bIsPanicking. Deliberately independent of bIsAiming/CrosshairWidgetInstance — visible even with
	// eyes closed (a pure post-process camera fade, unrelated to this Slate/UMG layer) and regardless of
	// aim state, since the whole point is this reads clearly while the world itself is faded near-black.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HUD|Breathing")
	float BreathingRingMaxRadius = 70.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HUD|Breathing")
	float BreathingRingTargetRadius = 24.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HUD|Breathing")
	float BreathingRingThickness = 3.0f;

	// Neutral ring/text color outside the brief post-result flash window below.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HUD|Breathing")
	FLinearColor BreathingNeutralColor = FLinearColor(0.9f, 0.9f, 0.9f, 0.9f);

	// Ring/text color for BreathingFlashDuration seconds after a successful nose-breath
	// (ABaseCharacter::bLastBreathWasHit).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HUD|Breathing")
	FLinearColor BreathingHitColor = FLinearColor(0.3f, 1.0f, 0.4f, 1.0f);

	// Ring/text color for BreathingFlashDuration seconds after a mouth-gasp.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HUD|Breathing")
	FLinearColor BreathingMissColor = FLinearColor(1.0f, 0.2f, 0.2f, 1.0f);

	// Seconds the hit/miss flash (BreathingHitColor/BreathingMissColor) stays before fading back to
	// BreathingNeutralColor — measured against ABaseCharacter::LastBreathResultTime.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HUD|Breathing")
	float BreathingFlashDuration = 0.35f;

	// Explicitly sets the owning character, ahead of this widget's first NativeConstruct() — call right
	// after CreateWidget(), before AddToViewport(). NativeConstruct() only falls back to resolving
	// GetOwningPlayerPawn() itself if this was never called (e.g. a Blueprint-driven CreateWidget flow),
	// because GetOwningPlayerPawn() (-> the owning APlayerController's GetPawn()) can't be relied on for
	// the GameMode's INITIAL pawn: ABaseCharacter creates this widget from PossessedBy() (via
	// InitializeLocalPlayerUI() — see its own comment), and AController::Possess() calls
	// InPawn->PossessedBy(this) BEFORE SetPawn(InPawn), so the controller's GetPawn() is still null/stale
	// at that exact moment — NativeConstruct() would otherwise silently resolve no owner at all.
	void SetOwnerCharacter(ABaseCharacter* InOwnerCharacter) { OwnerCharacter = InOwnerCharacter; }

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	UFUNCTION()
	void HandleHealthChanged(float NewHealth, float MaxHealth);

	UFUNCTION()
	void HandleInventoryUpdated();

	UFUNCTION()
	void HandleActiveSlotChanged(int32 NewIndex, int32 OldIndex);

	UFUNCTION()
	void HandleHighlightedItemChanged(class AHolyItem* NewHighlightedItem);

	// Draws one bottom-center quick-slot square at SlotIndex — extracted out of NativePaint() purely to
	// keep that function readable; not meant to be called from anywhere else. AllottedGeometry is passed
	// through from NativePaint() since every draw call must go through
	// AllottedGeometry.ToPaintGeometry() to correctly compose with this widget's own position/DPI scale
	// in the viewport (a raw FPaintGeometry(Position, Size, Scale) constructor treats Position as
	// absolute WINDOW space, not local-to-this-widget, which would draw everything in the wrong place).
	void PaintQuickSlot(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FVector2D& SlotPosition, int32 SlotIndex) const;

	// Draws the Breathing Rhythm Indicator screen-center — no-ops if !OwnerCharacter->bIsPanicking.
	// Deliberately reads OwnerCharacter's Panic state LIVE (ABaseCharacter::GetBreathBeatProximity()/
	// bLastBreathWasHit/LastBreathResultTime) rather than through the cached-and-delegate-refreshed
	// CachedHealth/CachedSlots pattern the rest of this widget uses — there's no sensible discrete event
	// to cache against for a continuously time-driven pulse animation, so this is the one place NativePaint
	// intentionally reads live state instead.
	void PaintBreathingIndicator(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements, int32 LayerId) const;

	// Draws Text twice — a 1px-offset black "shadow" copy first, then TextColor on top — the standard
	// fake-outline trick for raw Slate text (MakeText has no built-in shadow/outline param). Used for
	// every text label PaintQuickSlot() draws so item names/slot numbers/charge counts stay legible
	// against ANY background, not just the intended dark slot fill. The shadow's own alpha follows
	// TextColor's alpha, so it dims correctly too when TextColor is itself dimmed (see PaintQuickSlot()'s
	// Empty-slot handling).
	void DrawShadowedText(FSlateWindowElementList& OutDrawElements, const FGeometry& AllottedGeometry, int32 LayerId,
		const FVector2D& TextPosition, const FVector2D& TextSize, const FText& Text, const FSlateFontInfo& Font, const FLinearColor& TextColor) const;

	TWeakObjectPtr<ABaseCharacter> OwnerCharacter;

	// Pulled once in NativeConstruct() and kept in sync via HandleHealthChanged() — NativePaint() is
	// const and runs on the render thread's data-gathering pass, so it reads these cached values rather
	// than re-querying OwnerCharacter/InventoryComponent live every frame.
	float CachedHealth = 100.0f;
	float CachedMaxHealth = 100.0f;
	int32 CachedActiveSlotIndex = 0;
	TArray<FQuickSlotData> CachedSlots;
	FText CachedPromptText;
};

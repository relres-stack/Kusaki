// Fill out your copyright notice in the Description page of Project Settings.

#include "PlayerHUDWidget.h"
#include "BaseCharacter.h"
#include "HolyItem.h"
#include "Brushes/SlateColorBrush.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Framework/Application/SlateApplication.h"
#include "Fonts/FontMeasure.h"

void UPlayerHUDWidget::NativeConstruct()
{
	Super::NativeConstruct();

	CachedSlots.SetNum(UInventoryComponent::NumQuickSlots);

	// SetOwnerCharacter() already set this if the creator called it (see its own comment on why
	// GetOwningPlayerPawn() can't be trusted for the GameMode's initial pawn) — only fall back to
	// resolving it here for a Blueprint-driven CreateWidget flow that never called that setter.
	if (!OwnerCharacter.IsValid())
	{
		OwnerCharacter = Cast<ABaseCharacter>(GetOwningPlayerPawn());
	}
	if (!OwnerCharacter.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("%s: no owning ABaseCharacter — HUD will show default/empty state"), *GetName());
		return;
	}

	// Pull current values now (delegates only fire on the NEXT change, not retroactively) so the HUD
	// isn't blank/at default until something happens to trigger a broadcast.
	CachedHealth = OwnerCharacter->Health;
	CachedMaxHealth = OwnerCharacter->MaxHealth;
	OnHealthChangedBP(CachedHealth, CachedMaxHealth);

	OwnerCharacter->OnHealthChanged.AddDynamic(this, &UPlayerHUDWidget::HandleHealthChanged);
	OwnerCharacter->OnHighlightedItemChanged.AddDynamic(this, &UPlayerHUDWidget::HandleHighlightedItemChanged);

	if (IsValid(OwnerCharacter->InventoryComponent))
	{
		CachedActiveSlotIndex = OwnerCharacter->InventoryComponent->ActiveSlotIndex;
		HandleInventoryUpdated();

		OwnerCharacter->InventoryComponent->OnInventoryUpdated.AddDynamic(this, &UPlayerHUDWidget::HandleInventoryUpdated);
		OwnerCharacter->InventoryComponent->OnActiveSlotChanged.AddDynamic(this, &UPlayerHUDWidget::HandleActiveSlotChanged);
	}
}

void UPlayerHUDWidget::NativeDestruct()
{
	if (OwnerCharacter.IsValid())
	{
		OwnerCharacter->OnHealthChanged.RemoveDynamic(this, &UPlayerHUDWidget::HandleHealthChanged);
		OwnerCharacter->OnHighlightedItemChanged.RemoveDynamic(this, &UPlayerHUDWidget::HandleHighlightedItemChanged);

		if (IsValid(OwnerCharacter->InventoryComponent))
		{
			OwnerCharacter->InventoryComponent->OnInventoryUpdated.RemoveDynamic(this, &UPlayerHUDWidget::HandleInventoryUpdated);
			OwnerCharacter->InventoryComponent->OnActiveSlotChanged.RemoveDynamic(this, &UPlayerHUDWidget::HandleActiveSlotChanged);
		}
	}

	Super::NativeDestruct();
}

void UPlayerHUDWidget::HandleHealthChanged(float NewHealth, float NewMaxHealth)
{
	CachedHealth = NewHealth;
	CachedMaxHealth = NewMaxHealth;
	OnHealthChangedBP(CachedHealth, CachedMaxHealth);
}

void UPlayerHUDWidget::HandleInventoryUpdated()
{
	if (!OwnerCharacter.IsValid() || !IsValid(OwnerCharacter->InventoryComponent))
	{
		return;
	}

	for (int32 SlotIndex = 0; SlotIndex < UInventoryComponent::NumQuickSlots; ++SlotIndex)
	{
		CachedSlots[SlotIndex] = OwnerCharacter->InventoryComponent->GetSlotData(SlotIndex);
		OnQuickSlotUpdatedBP(SlotIndex, CachedSlots[SlotIndex]);
	}
}

void UPlayerHUDWidget::HandleActiveSlotChanged(int32 NewIndex, int32 OldIndex)
{
	CachedActiveSlotIndex = NewIndex;
	OnActiveSlotChangedBP(NewIndex, OldIndex);
}

void UPlayerHUDWidget::HandleHighlightedItemChanged(AHolyItem* NewHighlightedItem)
{
	CachedPromptText = OwnerCharacter.IsValid() ? FText::FromString(OwnerCharacter->BuildInteractionPromptText()) : FText::GetEmpty();
	OnInteractionPromptChangedBP(CachedPromptText, !CachedPromptText.IsEmpty());
}

int32 UPlayerHUDWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	LayerId = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);

	if (!bUseNativeDrawing)
	{
		return LayerId;
	}

	const FVector2D LocalSize = FVector2D(AllottedGeometry.GetLocalSize());
	const FSlateFontInfo LabelFont = FCoreStyle::GetDefaultFontStyle("Regular", 12);
	const TSharedRef<FSlateFontMeasure> FontMeasure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();

	// --- Health bar, bottom-left ---
	{
		const FVector2D BarPosition(HealthBarMargin.X, LocalSize.Y - HealthBarMargin.Y - HealthBarSize.Y);

		// Flat-colour rectangle: white brush, real colour passed as MakeBox()'s InTint (see PaintQuickSlot()).
		const FSlateColorBrush BackgroundBrush(FLinearColor::White);
		FSlateDrawElement::MakeBox(OutDrawElements, LayerId,
			AllottedGeometry.ToPaintGeometry(HealthBarSize, FSlateLayoutTransform(BarPosition)), &BackgroundBrush,
			ESlateDrawEffect::None, HealthBarBackgroundColor);

		const float HealthFraction = CachedMaxHealth > 0.0f ? FMath::Clamp(CachedHealth / CachedMaxHealth, 0.0f, 1.0f) : 0.0f;
		if (HealthFraction > 0.0f)
		{
			const FVector2D FillSize(HealthBarSize.X * HealthFraction, HealthBarSize.Y);
			const FSlateColorBrush FillBrush(FLinearColor::White);
			FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 1,
				AllottedGeometry.ToPaintGeometry(FillSize, FSlateLayoutTransform(BarPosition)), &FillBrush,
				ESlateDrawEffect::None, HealthBarFillColor);
		}

		// Bold text with a shadow so the readout stays legible over both the dark background and the red fill.
		const FSlateFontInfo HealthFont = FCoreStyle::GetDefaultFontStyle("Bold", 14);
		const FText HealthText = FText::FromString(FString::Printf(TEXT("%.0f / %.0f"), CachedHealth, CachedMaxHealth));
		const FVector2D TextSize = FontMeasure->Measure(HealthText, HealthFont);
		const FVector2D TextPosition = BarPosition + FVector2D((HealthBarSize.X - TextSize.X) * 0.5f, (HealthBarSize.Y - TextSize.Y) * 0.5f);
		DrawShadowedText(OutDrawElements, AllottedGeometry, LayerId + 2, TextPosition, TextSize, HealthText, HealthFont, HealthBarTextColor);
	}

	// --- Quick-slot bar, bottom-center ---
	{
		const float TotalWidth = (QuickSlotSize.X * UInventoryComponent::NumQuickSlots) + (QuickSlotSpacing * (UInventoryComponent::NumQuickSlots - 1));
		const float StartX = (LocalSize.X - TotalWidth) * 0.5f;
		const float SlotY = LocalSize.Y - QuickSlotBottomMargin - QuickSlotSize.Y;

		for (int32 SlotIndex = 0; SlotIndex < UInventoryComponent::NumQuickSlots; ++SlotIndex)
		{
			const FVector2D SlotPosition(StartX + SlotIndex * (QuickSlotSize.X + QuickSlotSpacing), SlotY);
			PaintQuickSlot(AllottedGeometry, OutDrawElements, LayerId + 3, SlotPosition, SlotIndex);
		}
	}

	// --- Interaction prompt fallback: only drawn here if this HUD has no valid OwnerCharacter (so
	// UCrosshairWidget, which normally owns this real estate — see its own header comment — never got a
	// chance to). Kept purely as a safety net, not the primary prompt display.
	if (!OwnerCharacter.IsValid() && !CachedPromptText.IsEmpty())
	{
		const FVector2D TextSize = FontMeasure->Measure(CachedPromptText, LabelFont);
		const FVector2D TextPosition = (LocalSize * 0.5f) - FVector2D(TextSize.X * 0.5f, -40.0f);
		FSlateDrawElement::MakeText(OutDrawElements, LayerId + 3,
			AllottedGeometry.ToPaintGeometry(TextSize, FSlateLayoutTransform(TextPosition)),
			CachedPromptText, LabelFont, ESlateDrawEffect::None, FLinearColor::White);
	}

	// --- Breathing Rhythm Indicator, screen-center (no-ops internally unless bIsPanicking) ---
	PaintBreathingIndicator(AllottedGeometry, OutDrawElements, LayerId + 4);

	return LayerId + 8;
}

void UPlayerHUDWidget::DrawShadowedText(FSlateWindowElementList& OutDrawElements, const FGeometry& AllottedGeometry, int32 LayerId,
	const FVector2D& TextPosition, const FVector2D& TextSize, const FText& Text, const FSlateFontInfo& Font, const FLinearColor& TextColor) const
{
	const FLinearColor ShadowColor(0.0f, 0.0f, 0.0f, 0.85f * TextColor.A);
	const FVector2D ShadowOffset(1.0f, 1.0f);

	FSlateDrawElement::MakeText(OutDrawElements, LayerId,
		AllottedGeometry.ToPaintGeometry(TextSize, FSlateLayoutTransform(TextPosition + ShadowOffset)),
		Text, Font, ESlateDrawEffect::None, ShadowColor);
	FSlateDrawElement::MakeText(OutDrawElements, LayerId + 1,
		AllottedGeometry.ToPaintGeometry(TextSize, FSlateLayoutTransform(TextPosition)),
		Text, Font, ESlateDrawEffect::None, TextColor);
}

void UPlayerHUDWidget::PaintQuickSlot(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FVector2D& SlotPosition, int32 SlotIndex) const
{
	const bool bIsActive = SlotIndex == CachedActiveSlotIndex;
	const FQuickSlotData& SlotData = CachedSlots.IsValidIndex(SlotIndex) ? CachedSlots[SlotIndex] : FQuickSlotData();
	const bool bIsEmpty = SlotData.SlotType == EQuickSlotType::Empty;

	// An Empty slot (nothing equipped, or the equipped light source was thrown/dropped/depleted — see
	// UInventoryComponent::RefreshLightSourceSlot()) is dimmed via a flat alpha multiplier on every
	// element below, so "this slot cleared" reads at a glance instead of only being inferable from the
	// missing fill bar/label/charge text.
	const float OpacityMultiplier = bIsEmpty ? EmptySlotOpacityMultiplier : 1.0f;

	// Background — brighter border color when this is the active slot, otherwise a flat dark box (no
	// separate border brush needed: draw a slightly larger box in the border color behind a smaller one
	// in the background color, same "outline via two nested boxes" trick UCrosshairWidget's dot uses).
	const float BorderThickness = bIsActive ? 3.0f : 1.0f;
	FLinearColor BorderColor = bIsActive ? QuickSlotActiveBorderColor : FLinearColor(0.3f, 0.3f, 0.3f, 0.9f);
	BorderColor.A *= OpacityMultiplier;

	// Flat-colour fills: the brush is plain white and the real colour goes in as MakeBox()'s InTint argument.
	// (MakeBox() paints with InTint, which defaults to white when omitted — that, not the renderer, was why
	// these boxes used to come out solid white whatever colour was configured.)
	const FSlateColorBrush BorderBrush(FLinearColor::White);
	FSlateDrawElement::MakeBox(OutDrawElements, LayerId,
		AllottedGeometry.ToPaintGeometry(QuickSlotSize + FVector2D(BorderThickness * 2.0f, BorderThickness * 2.0f),
			FSlateLayoutTransform(SlotPosition - FVector2D(BorderThickness, BorderThickness))),
		&BorderBrush, ESlateDrawEffect::None, BorderColor);

	FLinearColor BackgroundColor = QuickSlotBackgroundColor;
	BackgroundColor.A *= OpacityMultiplier;
	const FSlateColorBrush BackgroundBrush(FLinearColor::White);
	FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 1,
		AllottedGeometry.ToPaintGeometry(QuickSlotSize, FSlateLayoutTransform(SlotPosition)), &BackgroundBrush,
		ESlateDrawEffect::None, BackgroundColor);

	// Slot number (1-4) in the top-left corner, always shown, so the quick-slot bar reads clearly even
	// on an Empty/unpopulated slot. Bright white + shadow (not a mid-gray) for max contrast against the
	// dark background — see DrawShadowedText()'s own comment.
	FLinearColor SlotNumberColor = FLinearColor::White;
	SlotNumberColor.A *= OpacityMultiplier;
	const FSlateFontInfo SlotNumberFont = FCoreStyle::GetDefaultFontStyle("Regular", 10);
	const FText SlotNumberText = FText::AsNumber(SlotIndex + 1);
	DrawShadowedText(OutDrawElements, AllottedGeometry, LayerId + 3,
		SlotPosition + FVector2D(4.0f, 2.0f), FVector2D(12.0f, 12.0f), SlotNumberText, SlotNumberFont, SlotNumberColor);

	if (bIsEmpty)
	{
		return;
	}

	// Item name/label (e.g. "Candle"/"Censer"/"Flashlight"/"Moonshine" — see FQuickSlotData::DisplayName),
	// centered in the slot's vertical middle: between the slot-number row above and the fill bar below, so
	// equipping/changing what's in this slot is legible by name at a glance, not just inferable from the
	// fill bar's presence. Bold and sized up slightly (10, from an earlier 9) so it stands out clearly
	// even in this compact slot (QuickSlotSize defaults to 56x56) — a longer name (e.g. "Flashlight") may
	// still slightly outgrow the slot's width; left uncorrected (no wrap/scale-to-fit) since nothing else
	// in this native-drawn HUD clips text either.
	const FSlateFontInfo ItemNameFont = FCoreStyle::GetDefaultFontStyle("Bold", 10);
	const TSharedRef<FSlateFontMeasure> NameFontMeasure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	const FVector2D NameTextSize = NameFontMeasure->Measure(SlotData.DisplayName, ItemNameFont);
	const FVector2D NameTextPosition = SlotPosition + FVector2D((QuickSlotSize.X - NameTextSize.X) * 0.5f, (QuickSlotSize.Y - NameTextSize.Y) * 0.5f);
	DrawShadowedText(OutDrawElements, AllottedGeometry, LayerId + 3, NameTextPosition, NameTextSize, SlotData.DisplayName, ItemNameFont, QuickSlotItemNameColor);

	// Resource fill bar along the bottom edge of the slot — fuel/battery remaining, or boost-time
	// remaining for the Consumable slot (see UInventoryComponent::RefreshLightSourceSlot()/
	// RefreshConsumableSlot() for what FillFraction means per slot type).
	constexpr float FillBarHeight = 6.0f;
	const float FillWidth = QuickSlotSize.X * FMath::Clamp(SlotData.FillFraction, 0.0f, 1.0f);
	if (FillWidth > 0.0f)
	{
		const FVector2D FillPosition = SlotPosition + FVector2D(0.0f, QuickSlotSize.Y - FillBarHeight);
		const FSlateColorBrush FillBrush(FLinearColor::White);
		FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 2,
			AllottedGeometry.ToPaintGeometry(FVector2D(FillWidth, FillBarHeight), FSlateLayoutTransform(FillPosition)), &FillBrush,
			ESlateDrawEffect::None, QuickSlotFillColor);
	}

	// Numeric countdown (e.g. Moonshine's active buff/cooldown — see FQuickSlotData::SecondsRemaining's
	// own comment) along the bottom edge, ON TOP of the fill bar above.
	if (SlotData.SecondsRemaining > 0.0f)
	{
		const FText TimerText = FText::FromString(FString::Printf(TEXT("%.1fs"), SlotData.SecondsRemaining));
		const FSlateFontInfo TimerFont = FCoreStyle::GetDefaultFontStyle("Bold", 11);
		const TSharedRef<FSlateFontMeasure> TimerFontMeasure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
		const FVector2D TimerTextSize = TimerFontMeasure->Measure(TimerText, TimerFont);
		const FVector2D TimerTextPosition = SlotPosition + FVector2D((QuickSlotSize.X - TimerTextSize.X) * 0.5f, QuickSlotSize.Y - TimerTextSize.Y - 3.0f);
		DrawShadowedText(OutDrawElements, AllottedGeometry, LayerId + 3, TimerTextPosition, TimerTextSize, TimerText, TimerFont, QuickSlotTimerTextColor);
	}

	// Charge count (e.g. Moonshine's "3") in the slot's top-right corner, when applicable.
	const FSlateFontInfo LabelFont = FCoreStyle::GetDefaultFontStyle("Bold", 14);
	if (SlotData.MaxChargeCount > 0)
	{
		const FText ChargeText = FText::FromString(FString::Printf(TEXT("%d"), SlotData.ChargeCount));
		const TSharedRef<FSlateFontMeasure> FontMeasure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
		const FVector2D TextSize = FontMeasure->Measure(ChargeText, LabelFont);
		const FVector2D TextPosition = SlotPosition + FVector2D(QuickSlotSize.X - TextSize.X - 4.0f, 2.0f);
		DrawShadowedText(OutDrawElements, AllottedGeometry, LayerId + 3, TextPosition, TextSize, ChargeText, LabelFont, FLinearColor::White);
	}
}

void UPlayerHUDWidget::PaintBreathingIndicator(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements, int32 LayerId) const
{
	if (!OwnerCharacter.IsValid() || !OwnerCharacter->bIsPanicking)
	{
		return;
	}

	const FVector2D LocalSize = FVector2D(AllottedGeometry.GetLocalSize());
	const FVector2D Center = LocalSize * 0.5f;

	// Flash color: hit/miss for BreathingFlashDuration seconds after the last resolved beat, fading back
	// to neutral — measured against real elapsed time (GetWorld()->GetTimeSeconds()), not a cached value,
	// same "this one element reads OwnerCharacter live" reasoning as this function's own header comment.
	FLinearColor RingColor = BreathingNeutralColor;
	if (const UWorld* World = OwnerCharacter->GetWorld())
	{
		const float TimeSinceResult = World->GetTimeSeconds() - OwnerCharacter->LastBreathResultTime;
		if (OwnerCharacter->LastBreathResultTime >= 0.0f && TimeSinceResult >= 0.0f && TimeSinceResult < BreathingFlashDuration)
		{
			const FLinearColor FlashColor = OwnerCharacter->bLastBreathWasHit ? BreathingHitColor : BreathingMissColor;
			const float FlashAlpha = 1.0f - (TimeSinceResult / BreathingFlashDuration);
			RingColor = FMath::Lerp(BreathingNeutralColor, FlashColor, FlashAlpha);
		}
	}

	// The "inbound" ring shrinks from BreathingRingMaxRadius down to exactly BreathingRingTargetRadius as
	// GetBreathBeatProximity() goes 0 -> 1, landing precisely on the fixed target ring at the beat itself —
	// the standard "converging ring" rhythm-game readout.
	const float Proximity = OwnerCharacter->GetBreathBeatProximity();
	const float InboundRadius = FMath::Lerp(BreathingRingMaxRadius, BreathingRingTargetRadius, Proximity);

	auto DrawRing = [&OutDrawElements, &AllottedGeometry, &Center](int32 RingLayerId, float Radius, const FLinearColor& Color, float Thickness)
	{
		constexpr int32 NumSegments = 32;
		TArray<FVector2f> Points;
		Points.Reserve(NumSegments + 1);
		for (int32 i = 0; i <= NumSegments; ++i)
		{
			const float Angle = (2.0f * PI * i) / NumSegments;
			Points.Add(FVector2f(Center.X + Radius * FMath::Cos(Angle), Center.Y + Radius * FMath::Sin(Angle)));
		}
		FSlateDrawElement::MakeLines(OutDrawElements, RingLayerId, AllottedGeometry.ToPaintGeometry(), Points, ESlateDrawEffect::None, Color, true, Thickness);
	};

	// Fixed target ring first (so the inbound ring draws on top of it as it converges), then the inbound
	// ring itself.
	DrawRing(LayerId, BreathingRingTargetRadius, RingColor, BreathingRingThickness);
	DrawRing(LayerId + 1, InboundRadius, RingColor, BreathingRingThickness);

	// Text companion — PanicMeter as a plain number, so it stays readable even if the ring is hard to see.
	const FSlateFontInfo PanicFont = FCoreStyle::GetDefaultFontStyle("Bold", 16);
	const TSharedRef<FSlateFontMeasure> FontMeasure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	const FText PanicText = FText::FromString(FString::Printf(TEXT("PANIC: %.0f / %.0f — BREATHE (Space)"),
		OwnerCharacter->PanicMeter, OwnerCharacter->PanicMeterMax));
	const FVector2D PanicTextSize = FontMeasure->Measure(PanicText, PanicFont);
	const FVector2D PanicTextPosition = Center + FVector2D(-PanicTextSize.X * 0.5f, BreathingRingMaxRadius + 12.0f);
	DrawShadowedText(OutDrawElements, AllottedGeometry, LayerId + 2, PanicTextPosition, PanicTextSize, PanicText, PanicFont, RingColor);
}

// Fill out your copyright notice in the Description page of Project Settings.

#include "ControlsHintWidget.h"
#include "BaseCharacter.h"
#include "Brushes/SlateColorBrush.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Framework/Application/SlateApplication.h"
#include "Fonts/FontMeasure.h"
#include "GameFramework/PlayerController.h"

void UControlsHintWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// SetOwnerCharacter() normally already ran — fallback only for a Blueprint-driven CreateWidget flow.
	if (!OwnerCharacter.IsValid())
	{
		OwnerCharacter = Cast<ABaseCharacter>(GetOwningPlayerPawn());
	}

	CachedHints.Reset();
	if (OwnerCharacter.IsValid())
	{
		OwnerCharacter->GetControlHints(CachedHints);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("%s: no owning ABaseCharacter — controls list will be empty"), *GetName());
	}
}

void UControlsHintWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	const APlayerController* PC = GetOwningPlayer();
	if (!IsValid(PC) || !ToggleKey.IsValid())
	{
		return;
	}

	// Edge-triggered: one press flips the list, holding the key does nothing more.
	const bool bIsToggleKeyDown = PC->IsInputKeyDown(ToggleKey);
	if (bIsToggleKeyDown && !bWasToggleKeyDown)
	{
		bExpanded = !bExpanded;
	}
	bWasToggleKeyDown = bIsToggleKeyDown;
}

int32 UControlsHintWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	LayerId = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);

	if (!OwnerCharacter.IsValid() || CachedHints.Num() == 0)
	{
		return LayerId;
	}

	const FVector2D LocalSize = FVector2D(AllottedGeometry.GetLocalSize());
	const TSharedRef<FSlateFontMeasure> FontMeasure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();

	const FSlateFontInfo TitleFont = FCoreStyle::GetDefaultFontStyle("Bold", FontSize + 1);
	const FSlateFontInfo SectionFont = FCoreStyle::GetDefaultFontStyle("Bold", FontSize);
	const FSlateFontInfo KeyFont = FCoreStyle::GetDefaultFontStyle("Bold", FontSize);
	const FSlateFontInfo DescriptionFont = FCoreStyle::GetDefaultFontStyle("Regular", FontSize);

	const FString ToggleKeyName = ToggleKey.GetDisplayName().ToString();
	const FText TitleText = bExpanded
		? FText::FromString(FString::Printf(TEXT("УПРАВЛЕНИЕ   (%s — скрыть)"), *ToggleKeyName))
		: FText::FromString(FString::Printf(TEXT("%s — показать управление"), *ToggleKeyName));
	const FVector2D TitleSize = FontMeasure->Measure(TitleText, TitleFont);

	// --- Pass 1: measure columns and total height ---
	double KeyColumnWidth = 0.0;
	double DescriptionColumnWidth = 0.0;
	double SectionWidth = 0.0;
	double ContentHeight = TitleSize.Y;

	if (bExpanded)
	{
		for (int32 Index = 0; Index < CachedHints.Num(); ++Index)
		{
			const FControlHint& Hint = CachedHints[Index];
			ContentHeight += RowSpacing;
			if (Hint.IsSection())
			{
				const FVector2D Size = FontMeasure->Measure(Hint.Description, SectionFont);
				SectionWidth = FMath::Max(SectionWidth, Size.X);
				ContentHeight += SectionSpacing + Size.Y;
			}
			else
			{
				const FVector2D KeySize = FontMeasure->Measure(Hint.Key, KeyFont);
				const FVector2D DescriptionSize = FontMeasure->Measure(Hint.Description, DescriptionFont);
				KeyColumnWidth = FMath::Max(KeyColumnWidth, KeySize.X);
				DescriptionColumnWidth = FMath::Max(DescriptionColumnWidth, DescriptionSize.X);
				ContentHeight += FMath::Max(KeySize.Y, DescriptionSize.Y);
			}
		}
	}

	const double RowsWidth = bExpanded ? KeyColumnWidth + ColumnGap + DescriptionColumnWidth : 0.0;
	const double ContentWidth = FMath::Max3(TitleSize.X, RowsWidth, SectionWidth);
	const FVector2D PanelSize(ContentWidth + PanelPadding.X * 2.0f, ContentHeight + PanelPadding.Y * 2.0f);
	const FVector2D PanelPosition(LocalSize.X - ScreenMargin.X - PanelSize.X, ScreenMargin.Y);

	// --- Background (white brush, real colour as MakeBox()'s InTint — see UPlayerHUDWidget::PaintQuickSlot()) ---
	const FSlateColorBrush BackgroundBrush(FLinearColor::White);
	FSlateDrawElement::MakeBox(OutDrawElements, LayerId,
		AllottedGeometry.ToPaintGeometry(PanelSize, FSlateLayoutTransform(PanelPosition)), &BackgroundBrush,
		ESlateDrawEffect::None, BackgroundColor);

	// --- Pass 2: draw ---
	const int32 TextLayer = LayerId + 1;
	const double ContentLeft = PanelPosition.X + PanelPadding.X;
	double CursorY = PanelPosition.Y + PanelPadding.Y;

	DrawShadowedText(OutDrawElements, AllottedGeometry, TextLayer, FVector2D(ContentLeft, CursorY), TitleSize, TitleText, TitleFont, TitleColor);
	CursorY += TitleSize.Y;

	if (bExpanded)
	{
		for (int32 Index = 0; Index < CachedHints.Num(); ++Index)
		{
			const FControlHint& Hint = CachedHints[Index];
			CursorY += RowSpacing;
			if (Hint.IsSection())
			{
				CursorY += SectionSpacing;
				const FVector2D Size = FontMeasure->Measure(Hint.Description, SectionFont);
				DrawShadowedText(OutDrawElements, AllottedGeometry, TextLayer, FVector2D(ContentLeft, CursorY), Size, Hint.Description, SectionFont, SectionColor);
				CursorY += Size.Y;
			}
			else
			{
				const FVector2D KeySize = FontMeasure->Measure(Hint.Key, KeyFont);
				const FVector2D DescriptionSize = FontMeasure->Measure(Hint.Description, DescriptionFont);
				DrawShadowedText(OutDrawElements, AllottedGeometry, TextLayer, FVector2D(ContentLeft, CursorY), KeySize, Hint.Key, KeyFont, KeyColor);
				DrawShadowedText(OutDrawElements, AllottedGeometry, TextLayer, FVector2D(ContentLeft + KeyColumnWidth + ColumnGap, CursorY),
					DescriptionSize, Hint.Description, DescriptionFont, DescriptionColor);
				CursorY += FMath::Max(KeySize.Y, DescriptionSize.Y);
			}
		}
	}

	return TextLayer + 2;
}

void UControlsHintWidget::DrawShadowedText(FSlateWindowElementList& OutDrawElements, const FGeometry& AllottedGeometry, int32 LayerId,
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

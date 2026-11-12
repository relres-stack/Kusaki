// Fill out your copyright notice in the Description page of Project Settings.

#include "CrosshairWidget.h"
#include "Brushes/SlateColorBrush.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Framework/Application/SlateApplication.h"
#include "Fonts/FontMeasure.h"

int32 UCrosshairWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	LayerId = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);

	// Centered on THIS widget's own local geometry, not the viewport directly — ABaseCharacter adds this
	// widget full-screen (anchored 0,0 to 1,1, no padding, see its BeginPlay()), so the widget's local
	// center IS the screen center regardless of resolution/aspect ratio.
	const FVector2D Center = FVector2D(AllottedGeometry.GetLocalSize()) * 0.5f;

	// bShowDot gates the dot ONLY — the prompt text below is independent (see its own header comment),
	// so a "not currently aiming, but standing over a pickup-able item" frame still shows the prompt
	// with no dot at all.
	if (bShowDot)
	{
		// Outline drawn first (lower layer), slightly larger, so the smaller colored dot on top of it
		// reads with a dark edge against any background. The actual paint color comes from the explicit
		// InTint argument below, NOT from the brush itself — MakeBox() defaults InTint to
		// FLinearColor::White when omitted, which is why this used to render solid white regardless of
		// OutlineColor/DotColor no matter what brush type was used.
		const float OutlineRadius = DotRadius + OutlineThickness;
		const FSlateColorBrush OutlineBrush(OutlineColor);
		FSlateDrawElement::MakeBox(
			OutDrawElements, LayerId,
			AllottedGeometry.ToPaintGeometry(FVector2D(OutlineRadius * 2.0f, OutlineRadius * 2.0f),
				FSlateLayoutTransform(Center - FVector2D(OutlineRadius, OutlineRadius))),
			&OutlineBrush, ESlateDrawEffect::None, OutlineColor);

		const FSlateColorBrush DotBrush(DotColor);
		FSlateDrawElement::MakeBox(
			OutDrawElements, LayerId + 1,
			AllottedGeometry.ToPaintGeometry(FVector2D(DotRadius * 2.0f, DotRadius * 2.0f),
				FSlateLayoutTransform(Center - FVector2D(DotRadius, DotRadius))),
			&DotBrush, ESlateDrawEffect::None, DotColor);
	}

	if (!PromptText.IsEmpty())
	{
		// FCoreStyle's default font is always available — no font asset needed, same "zero external
		// dependency" approach as FSlateColorBrush above.
		const FSlateFontInfo PromptFont = FCoreStyle::GetDefaultFontStyle("Regular", 14);

		// Measured (not guessed) so the prompt is genuinely centered under the dot regardless of string
		// length — a fixed-offset approximation would drift noticeably between a short and long prompt.
		const TSharedRef<FSlateFontMeasure> FontMeasure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
		const FVector2D TextSize = FontMeasure->Measure(PromptText, PromptFont);

		const FVector2D TextPosition = Center + FVector2D(-TextSize.X * 0.5f, PromptTextOffsetY);

		FSlateDrawElement::MakeText(
			OutDrawElements, LayerId + 2,
			AllottedGeometry.ToPaintGeometry(TextSize, FSlateLayoutTransform(TextPosition)),
			PromptText, PromptFont, ESlateDrawEffect::None, PromptTextColor);
	}

	return LayerId + 3;
}

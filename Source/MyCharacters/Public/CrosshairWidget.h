// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CrosshairWidget.generated.h"

// Fully native (no Widget Blueprint asset required) screen-space crosshair dot — replaces
// ABaseCharacter's old 3D world-space "flying ball" reticle (its former AimReticleSphere/AimReticleLine
// mesh components, which followed the aim trace's hit point through the world) with a real 2D HUD
// element fixed at the center of the screen. Drawn in NativePaint() via FSlateRoundedBoxBrush — a
// genuinely round, anti-aliased Slate primitive that needs no texture/material asset at all (a corner
// radius equal to half the box's own size turns a square box into a circle). ABaseCharacter creates and
// owns exactly one instance (see its BeginPlay()/UpdateAimCrosshair()), which sets DotColor every frame
// while aiming based on what the center-screen aim trace is currently hitting, and toggles this widget's
// visibility to match bIsAiming. Also draws the interaction-prompt text (e.g. "[E] Pick Up Candle") just
// below the dot — see PromptText — since that's naturally "attached to the crosshair" real estate; this
// part is independent of bIsAiming/bShowDot, so the prompt still shows even while not aiming.
UCLASS()
class MYCHARACTERS_API UCrosshairWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// Current fill color of the dot. ABaseCharacter::UpdateAimCrosshair() sets this every frame: green
	// while aimed at a mountable ABaseMonster, red while aimed at a target/item, its own default
	// (neutral white/light grey) otherwise.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crosshair")
	FLinearColor DotColor = FLinearColor::White;

	// Radius (in screen pixels) of the filled dot itself.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crosshair")
	float DotRadius = 4.0f;

	// Extra radius (in screen pixels), beyond DotRadius, of the dark outline drawn behind the dot on
	// every side — what keeps the dot visible against a bright or busy background regardless of its
	// current fill color.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crosshair")
	float OutlineThickness = 1.5f;

	// Outline color drawn behind the dot. Near-black/semi-transparent by default so it reads as an edge
	// under any of the three fill colors DotColor can be set to.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crosshair")
	FLinearColor OutlineColor = FLinearColor(0.0f, 0.0f, 0.0f, 0.85f);

	// Whether to draw the dot itself this frame — ABaseCharacter::Tick() sets this to bIsAiming. Kept
	// separate from this whole widget's own SetVisibility() (which ALSO needs to stay visible whenever
	// PromptText is non-empty, independent of aiming) so the dot and the prompt can show/hide on their
	// own, independent conditions within a single always-mounted widget.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crosshair")
	bool bShowDot = false;

	// Interaction-prompt text drawn just below the dot's position (e.g. "[E] Pick Up Candle") — empty
	// means nothing is drawn. Set by ABaseCharacter::Tick() from BuildInteractionPromptText() whenever
	// OnHighlightedItemChanged fires (well, every Tick() for simplicity — cheap string work).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crosshair")
	FText PromptText;

	// Prompt text color.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crosshair")
	FLinearColor PromptTextColor = FLinearColor(0.95f, 0.95f, 0.95f);

	// Pixels below the dot's outline the prompt text baseline starts at.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crosshair")
	float PromptTextOffsetY = 20.0f;

protected:
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
};

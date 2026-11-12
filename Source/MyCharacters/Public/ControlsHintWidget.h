// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ControlHint.h"
#include "InputCoreTypes.h"
#include "ControlsHintWidget.generated.h"

class ABaseCharacter;

// Fully native (no Widget Blueprint asset required) list of the hero's controls, drawn in the top-right
// corner from the moment the hero is possessed — meant for testers, so they know what each key does.
// The list itself comes from OwnerCharacter->GetControlHints() (ABaseCharacter + each hero's override),
// read once in NativeConstruct(); this widget only draws it.
//
// ToggleKey (H by default) collapses the list to a one-line reminder and expands it back. The key is
// polled in NativeTick() (IsInputKeyDown edge detection, same reliable approach ABaseCharacter::Tick()
// uses for aiming), so it needs no input binding on any character and cannot clash with one.
//
// Created by ABaseCharacter::InitializeLocalPlayerUI(), same pattern as UPlayerHUDWidget.
UCLASS()
class MYCHARACTERS_API UControlsHintWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// Call right after CreateWidget(), before AddToViewport() — same reason as
	// UPlayerHUDWidget::SetOwnerCharacter().
	void SetOwnerCharacter(ABaseCharacter* InOwnerCharacter) { OwnerCharacter = InOwnerCharacter; }

	// true: full list. false: only a one-line "press H to show controls" reminder.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Controls Hint")
	bool bExpanded = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Controls Hint")
	FKey ToggleKey = EKeys::H;

	// Distance from the top-right corner of the screen to the panel.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Controls Hint|Layout")
	FVector2D ScreenMargin = FVector2D(20.0f, 20.0f);

	// Inner padding of the panel background.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Controls Hint|Layout")
	FVector2D PanelPadding = FVector2D(12.0f, 10.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Controls Hint|Layout")
	float RowSpacing = 2.0f;

	// Extra space above each section heading (except the first row).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Controls Hint|Layout")
	float SectionSpacing = 8.0f;

	// Gap between the key column and the description column.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Controls Hint|Layout")
	float ColumnGap = 12.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Controls Hint|Layout", meta = (ClampMin = "6", ClampMax = "32"))
	int32 FontSize = 11;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Controls Hint|Colors")
	FLinearColor BackgroundColor = FLinearColor(0.0f, 0.0f, 0.0f, 0.55f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Controls Hint|Colors")
	FLinearColor TitleColor = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Controls Hint|Colors")
	FLinearColor SectionColor = FLinearColor(0.55f, 0.8f, 1.0f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Controls Hint|Colors")
	FLinearColor KeyColor = FLinearColor(1.0f, 0.8f, 0.15f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Controls Hint|Colors")
	FLinearColor DescriptionColor = FLinearColor(0.92f, 0.92f, 0.92f, 1.0f);

protected:
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	// Text with a 1px black shadow — same trick as UPlayerHUDWidget::DrawShadowedText().
	void DrawShadowedText(FSlateWindowElementList& OutDrawElements, const FGeometry& AllottedGeometry, int32 LayerId,
		const FVector2D& TextPosition, const FVector2D& TextSize, const FText& Text, const FSlateFontInfo& Font, const FLinearColor& TextColor) const;

	TWeakObjectPtr<ABaseCharacter> OwnerCharacter;

	// Read once from OwnerCharacter->GetControlHints() in NativeConstruct() — the list is fixed per hero class.
	TArray<FControlHint> CachedHints;

	bool bWasToggleKeyDown = false;
};

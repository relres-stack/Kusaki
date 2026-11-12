// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "ControlHint.generated.h"

// One line of the on-screen controls list (see UControlsHintWidget): "Key — what it does".
// A line with an empty Key is drawn as a section heading (Description is the heading text) —
// build those with FControlHint::Section().
//
// Filled by ABaseCharacter::GetControlHints() and its per-hero overrides, which live right next to
// each class's SetupPlayerInputComponent(): whenever a key binding is added/changed there, update the
// matching line in GetControlHints() of the same class.
USTRUCT(BlueprintType)
struct MYCHARACTERS_API FControlHint
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Controls")
	FText Key;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Controls")
	FText Description;

	FControlHint() {}

	FControlHint(const FText& InKey, const FText& InDescription)
		: Key(InKey)
		, Description(InDescription)
	{
	}

	static FControlHint Section(const FText& Title)
	{
		return FControlHint(FText::GetEmpty(), Title);
	}

	bool IsSection() const { return Key.IsEmpty(); }
};

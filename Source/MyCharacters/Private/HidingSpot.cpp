// Fill out your copyright notice in the Description page of Project Settings.

#include "HidingSpot.h"
#include "BaseCharacter.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Kismet/GameplayStatics.h"

AHidingSpot::AHidingSpot()
{
	PrimaryActorTick.bCanEverTick = true;

	SpotMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SpotMesh"));
	RootComponent = SpotMesh;

	// Same "Press E" placeholder prompt approach as ABinocularsPickup::PromptText.
	PromptText = CreateDefaultSubobject<UTextRenderComponent>(TEXT("PromptText"));
	PromptText->SetupAttachment(RootComponent);
	PromptText->SetRelativeLocation(FVector(0.0f, 0.0f, 60.0f));
	PromptText->SetText(FText::FromString(TEXT("Press E")));
	PromptText->SetTextRenderColor(FColor::Yellow);
	PromptText->SetWorldSize(24.0f);
	PromptText->SetHorizontalAlignment(EHTA_Center);
	PromptText->SetVerticalAlignment(EVRTA_TextCenter);
}

void AHidingSpot::BeginPlay()
{
	Super::BeginPlay();

	if (IsValid(PromptText))
	{
		PromptText->SetVisibility(false);
	}
}

void AHidingSpot::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (!IsValid(PromptText))
	{
		return;
	}

	ABaseCharacter* Player = Cast<ABaseCharacter>(UGameplayStatics::GetPlayerCharacter(this, 0));
	const bool bPlayerNearby = IsValid(Player) &&
		FVector::Dist(Player->GetActorLocation(), GetActorLocation()) <= PromptDisplayRadius;

	PromptText->SetVisibility(bPlayerNearby && !bOccupied);
}

// Fill out your copyright notice in the Description page of Project Settings.

#include "BinocularsPickup.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"
#include "Engine/Engine.h"

ABinocularsPickup::ABinocularsPickup()
{
	PrimaryActorTick.bCanEverTick = false;

	USceneComponent* SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = SceneRoot;

	BinocularsMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BinocularsMesh"));
	BinocularsMesh->SetupAttachment(RootComponent);
	BinocularsMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	BinocularsMesh->SetCollisionObjectType(ECC_WorldStatic);
	BinocularsMesh->SetCollisionResponseToAllChannels(ECR_Ignore);
	BinocularsMesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);

	// Placeholder visual — same "working default before a real asset exists" precedent as
	// ABearTrap::TrapMesh/AHolyItem's Candle placeholder.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMeshFinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (CylinderMeshFinder.Succeeded())
	{
		BinocularsMesh->SetStaticMesh(CylinderMeshFinder.Object);
	}
	BinocularsMesh->SetRelativeScale3D(FVector(0.3f, 0.3f, 0.2f));

	PromptText = CreateDefaultSubobject<UTextRenderComponent>(TEXT("PromptText"));
	PromptText->SetupAttachment(RootComponent);
	PromptText->SetRelativeLocation(FVector(0.0f, 0.0f, 60.0f));
	PromptText->SetText(FText::FromString(TEXT("Press E")));
	PromptText->SetTextRenderColor(FColor::Yellow);
	PromptText->SetWorldSize(24.0f);
	PromptText->SetHorizontalAlignment(EHTA_Center);
	PromptText->SetVerticalAlignment(EVRTA_TextCenter);
}

void ABinocularsPickup::BeginPlay()
{
	Super::BeginPlay();

	if (IsValid(PromptText))
	{
		PromptText->SetVisibility(!bHasBeenUsed);
	}
}

void ABinocularsPickup::LookThroughBinoculars()
{
	if (bHasBeenUsed)
	{
		return;
	}
	bHasBeenUsed = true;

	if (IsValid(PromptText))
	{
		PromptText->SetVisibility(false);
	}

	UE_LOG(LogTemp, Warning, TEXT("%s: looked through binoculars — %d steps to target"), *GetName(), DisplayedStepCount);
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(270, 5.0f, FColor::Cyan,
			FString::Printf(TEXT("%d STEPS TO TARGET"), DisplayedStepCount));
	}

	for (AActor* Marker : PhantomTrailMarkers)
	{
		if (IsValid(Marker))
		{
			Marker->SetActorHiddenInGame(false);
		}
	}

	// The binoculars themselves visually "put away" — placeholder for a real holster/belt-attach later,
	// see this class's own header comment.
	SetActorHiddenInGame(true);
}

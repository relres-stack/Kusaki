// Fill out your copyright notice in the Description page of Project Settings.

#include "HookPoint.h"
#include "RiderCharacter.h"
#include "Components/RiderHookComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

AHookPoint::AHookPoint()
{
	PrimaryActorTick.bCanEverTick = true;

	HookMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("HookMesh"));
	RootComponent = HookMesh;

	HookMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	HookMesh->SetCollisionObjectType(ECC_WorldStatic);
	HookMesh->SetCollisionResponseToAllChannels(ECR_Ignore);
	HookMesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);

	// Placeholder visual — same "working default before a real asset exists" precedent as
	// ABearTrap::TrapMesh/ABinocularsPickup::BinocularsMesh.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMeshFinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (CylinderMeshFinder.Succeeded())
	{
		HookMesh->SetStaticMesh(CylinderMeshFinder.Object);
	}
	HookMesh->SetRelativeScale3D(FVector(0.2f, 0.2f, 0.4f));
}

void AHookPoint::BeginPlay()
{
	Super::BeginPlay();

	if (IsValid(HookMesh) && HookMesh->GetMaterial(0))
	{
		HookDynamicMaterial = HookMesh->CreateDynamicMaterialInstance(0);
	}
}

void AHookPoint::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (!IsValid(HookDynamicMaterial))
	{
		return;
	}

	APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(this, 0);
	ARiderCharacter* Rider = Cast<ARiderCharacter>(PlayerPawn);
	const bool bRiderMountedNearby = IsValid(Rider) && Rider->IsMounted()
		&& IsValid(Rider->HookComponent)
		&& FVector::Dist(Rider->GetActorLocation(), GetActorLocation()) <= Rider->HookComponent->HookGrappleRange;

	if (bRiderMountedNearby)
	{
		const float Pulse = (FMath::Sin(GetWorld()->GetTimeSeconds() * HighlightPulseSpeed) + 1.0f) * 0.5f; // 0..1
		HookDynamicMaterial->SetVectorParameterValue(TEXT("EmissiveColor"), HighlightColor * FMath::Lerp(0.5f, 3.0f, Pulse));
	}
	else
	{
		HookDynamicMaterial->SetVectorParameterValue(TEXT("EmissiveColor"), FLinearColor::Black);
	}
}

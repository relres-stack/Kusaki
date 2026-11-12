// Fill out your copyright notice in the Description page of Project Settings.

#include "DrunkNPCSpawner.h"
#include "DrunkNPC.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "TimerManager.h"

ADrunkNPCSpawner::ADrunkNPCSpawner()
{
	PrimaryActorTick.bCanEverTick = false;

	PreviewMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PreviewMesh"));
	RootComponent = PreviewMesh;
	PreviewMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void ADrunkNPCSpawner::BeginPlay()
{
	Super::BeginPlay();

	SpawnNPC();
}

void ADrunkNPCSpawner::OnNPCConsumed()
{
	UE_LOG(LogTemp, Warning, TEXT("%s: NPC consumed — respawning in %.1fs"), *GetName(), RespawnDelay);
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(1011, 3.0f, FColor::Orange,
			FString::Printf(TEXT("Decoy spawner: next NPC in %.1fs"), RespawnDelay));
	}

	GetWorldTimerManager().SetTimer(RespawnTimerHandle, this, &ADrunkNPCSpawner::SpawnNPC, RespawnDelay, false);
}

void ADrunkNPCSpawner::SpawnNPC()
{
	if (!NPCClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("%s: SpawnNPC() — no NPCClass assigned, nothing to spawn"), *GetName());
		return;
	}

	FVector SpawnLocation = GetActorLocation();
	if (RespawnRadius > 0.0f)
	{
		const FVector2D RandomOffset = FMath::RandPointInCircle(RespawnRadius);
		SpawnLocation += FVector(RandomOffset.X, RandomOffset.Y, 0.0f);
	}

	ADrunkNPC* NewNPC = GetWorld()->SpawnActor<ADrunkNPC>(NPCClass, SpawnLocation, GetActorRotation());
	if (IsValid(NewNPC))
	{
		NewNPC->OwningSpawner = this;
	}

	UE_LOG(LogTemp, Warning, TEXT("%s: spawned new NPC at %s"), *GetName(), *SpawnLocation.ToCompactString());
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(1012, 3.0f, FColor::Green, TEXT("Decoy spawner: new NPC ready"));
	}
}

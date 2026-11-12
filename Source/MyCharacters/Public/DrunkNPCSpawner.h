// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DrunkNPCSpawner.generated.h"

class UStaticMeshComponent;
class ADrunkNPC;

// Designer-placed spawn point for the "спаивание НПС-собутыльников" mechanic (Roadmap Фаза 3, п.4) —
// same "world prop, placed by hand, no runtime generation" precedent as ABinocularsPickup/AHookPoint.
// Spawns one ADrunkNPC on BeginPlay(), and again RespawnDelay seconds after the previous one calls
// OnNPCConsumed() (having been fed to monsters and "eaten" — see ADrunkNPC's own header comment for the
// full lure cycle). A plain AActor with an optional preview mesh, not a component — this is a world
// object, not a character ability.
UCLASS()
class MYCHARACTERS_API ADrunkNPCSpawner : public AActor
{
	GENERATED_BODY()

public:
	ADrunkNPCSpawner();

	// Optional editor-preview mesh — purely visual, no collision logic depends on it. Left unset
	// (nullptr static mesh) by default; a designer can assign one on a BP subclass if a visible marker
	// in the level is useful.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Drunk NPC Spawner")
	TObjectPtr<UStaticMeshComponent> PreviewMesh;

	// Which ADrunkNPC subclass to spawn. Left unset (None) has no effect — SpawnNPC() silently no-ops
	// without a class assigned, same "safe without designer setup yet" convention as this project's
	// other optional TSubclassOf hooks (e.g. ARiderCharacter::StartingCenserClass).
	UPROPERTY(EditAnywhere, Category = "Drunk NPC Spawner")
	TSubclassOf<ADrunkNPC> NPCClass;

	// Seconds between OnNPCConsumed() firing and the next ADrunkNPC appearing.
	UPROPERTY(EditAnywhere, Category = "Drunk NPC Spawner")
	float RespawnDelay = 120.0f;

	// 0 = spawn exactly at this actor's own location; >0 = a random point within this radius (2D,
	// around this actor's location) each time SpawnNPC() runs.
	UPROPERTY(EditAnywhere, Category = "Drunk NPC Spawner")
	float RespawnRadius = 0.0f;

	// Called by the currently-live ADrunkNPC right before it destroys itself (see
	// ADrunkNPC::OnSurviveTimeExpired()) — arms the RespawnDelay timer for the next spawn.
	UFUNCTION(BlueprintCallable, Category = "Drunk NPC Spawner")
	void OnNPCConsumed();

protected:
	virtual void BeginPlay() override;

private:
	// Shared by BeginPlay() (the very first NPC) and the RespawnTimerHandle callback (every subsequent
	// one) — computes the spawn transform (RespawnRadius applied, if any) and spawns NPCClass, wiring
	// its OwningSpawner back to this actor. No-op (logged) if NPCClass is unset.
	void SpawnNPC();

	FTimerHandle RespawnTimerHandle;
};

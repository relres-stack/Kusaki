// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "PrologueCheckpointSubsystem.generated.h"

class UPrologueScenarioData;

// Remembers the prologue's last checkpoint across level loads (task 04, step 2.9). The prologue restarts by
// reloading the level (APrologueDirector::RestartFromCheckpoint(), and a monster's catch already reloads it too —
// ABaseMonster::ReloadLevelAfterDeath()); everything in the level comes back as it was, and the director, starting
// again, asks this subsystem where to go on from. A game instance subsystem lives as long as the game instance:
// across level loads, but a new PIE session starts with none.
UCLASS()
class MYCHARACTERS_API UPrologueCheckpointSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	// Step StepIndex of Scenario is the last checkpoint reached.
	void SetCheckpoint(const UPrologueScenarioData* Scenario, int32 StepIndex);

	// The last checkpoint reached with this scenario, INDEX_NONE if none (or it was reached with another one).
	int32 GetCheckpoint(const UPrologueScenarioData* Scenario) const;

	// The prologue is over: a later reload starts it from the beginning.
	void ClearCheckpoint();

	// How many times the prologue has restarted in this session (debug readout).
	int32 GetRestartCount() const { return RestartCount; }
	void NoteRestart() { ++RestartCount; }

private:
	FSoftObjectPath ScenarioPath;
	int32 StepIndex = INDEX_NONE;
	int32 RestartCount = 0;
};

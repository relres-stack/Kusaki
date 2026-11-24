// Fill out your copyright notice in the Description page of Project Settings.

#include "PrologueCheckpointSubsystem.h"

#include "PrologueScenarioData.h"

void UPrologueCheckpointSubsystem::SetCheckpoint(const UPrologueScenarioData* Scenario, int32 InStepIndex)
{
	ScenarioPath = FSoftObjectPath(Scenario);
	StepIndex = InStepIndex;
}

int32 UPrologueCheckpointSubsystem::GetCheckpoint(const UPrologueScenarioData* Scenario) const
{
	return (IsValid(Scenario) && ScenarioPath == FSoftObjectPath(Scenario)) ? StepIndex : INDEX_NONE;
}

void UPrologueCheckpointSubsystem::ClearCheckpoint()
{
	ScenarioPath.Reset();
	StepIndex = INDEX_NONE;
}

// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/Tasks/BTTask_BlackboardBase.h"
#include "BTTask_FindShadowPoint.generated.h"

// Behavior Tree task for ABaseMonster's sun-exposure flee behavior (see its OnStartFleeingToShadow()):
// samples random reachable NavMesh points around the executing pawn and line-traces each one toward the
// sun (via the level's ADayNightManager) looking for one a blocking obstruction (roof, rock, wall, etc.)
// actually shades — the first one found is written to TargetLocationKey and the task Succeeds, so a
// sibling MoveTo task can path there next.
UCLASS()
class MYCHARACTERS_API UBTTask_FindShadowPoint : public UBTTask_BlackboardBase
{
	GENERATED_BODY()

public:
	UBTTask_FindShadowPoint();

	// Radius (in units), centered on the executing pawn's current location, that candidate NavMesh points
	// are sampled from — see UNavigationSystemV1::GetRandomReachablePointInRadius() in ExecuteTask().
	UPROPERTY(EditAnywhere, Category = "Shadow Search")
	float SearchRadius = 1500.0f;

	// How many random NavMesh points to test (each one a separate sun-trace) before giving up and falling
	// back — see ExecuteTask()'s fallback comment for what happens if none of them qualify as full shadow.
	UPROPERTY(EditAnywhere, Category = "Shadow Search")
	int32 MaxTraceAttempts = 12;

	// Channel each candidate point's sun-trace runs on. ECC_Visibility by default, matching
	// ABaseMonster::SunTraceChannel — keep these two in sync (or point both at the same custom channel) so
	// a point this task calls "shadow" is actually the same thing CheckSunExposure() will later agree with
	// once the monster arrives there.
	UPROPERTY(EditAnywhere, Category = "Shadow Search")
	TEnumAsByte<ECollisionChannel> TraceChannel = ECC_Visibility;

	// Blackboard Vector key the found (or fallback) location is written to. A dedicated, explicitly named
	// key selector — NOT this class's inherited UBTTask_BlackboardBase::BlackboardKey, which this task
	// leaves unused; TargetLocationKey is the one that actually matters here, filtered (see the
	// constructor) to only accept Vector-typed Blackboard keys in the editor's key picker.
	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector TargetLocationKey;

	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;

protected:
	virtual FString GetStaticDescription() const override;
};

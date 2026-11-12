// Fill out your copyright notice in the Description page of Project Settings.

#include "BTTask_FindShadowPoint.h"
#include "AIController.h"
#include "GameFramework/Pawn.h"
#include "NavigationSystem.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "Kismet/GameplayStatics.h"
#include "DayNightManager.h"
#include "Engine/DirectionalLight.h"
#include "CollisionQueryParams.h"
#include "Engine/Engine.h"

UBTTask_FindShadowPoint::UBTTask_FindShadowPoint()
{
	NodeName = TEXT("Find Shadow Point");

	// Restricts the Blackboard key picker in the BT editor to Vector-typed keys only — same idiom AIModule's
	// own stock tasks use for a key selector that isn't the inherited base-class BlackboardKey (see this
	// property's header comment for why a separate, explicitly named key exists here at all).
	TargetLocationKey.AddVectorFilter(this, GET_MEMBER_NAME_CHECKED(UBTTask_FindShadowPoint, TargetLocationKey));
}

EBTNodeResult::Type UBTTask_FindShadowPoint::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	AAIController* AICon = OwnerComp.GetAIOwner();
	if (!IsValid(AICon))
	{
		return EBTNodeResult::Failed;
	}

	APawn* ControlledPawn = AICon->GetPawn();
	if (!IsValid(ControlledPawn))
	{
		return EBTNodeResult::Failed;
	}

	UBlackboardComponent* BlackboardComp = OwnerComp.GetBlackboardComponent();
	if (!IsValid(BlackboardComp))
	{
		return EBTNodeResult::Failed;
	}

	UWorld* World = ControlledPawn->GetWorld();
	if (!IsValid(World))
	{
		return EBTNodeResult::Failed;
	}

	const FVector Origin = ControlledPawn->GetActorLocation();

	// Without a DayNightManager (or its DirectionalLight), there's no "toward the sun" direction to trace
	// along at all — nothing this task does is meaningful, so fail outright rather than guessing a
	// direction.
	ADayNightManager* DayNightManager = Cast<ADayNightManager>(UGameplayStatics::GetActorOfClass(World, ADayNightManager::StaticClass()));
	if (!IsValid(DayNightManager) || !IsValid(DayNightManager->DirectionalLight))
	{
		UE_LOG(LogTemp, Warning, TEXT("%s: no ADayNightManager/DirectionalLight found — cannot find a shadow point"), *GetNodeName());
		return EBTNodeResult::Failed;
	}

	// A directional light's forward vector is the direction its RAYS travel (sun -> ground); the direction
	// TOWARD the sun itself — what each candidate point needs to trace along to check for obstruction — is
	// the opposite. Same convention as ABaseMonster::CheckSunExposure().
	const FVector DirectionToSun = -DayNightManager->DirectionalLight->GetActorForwardVector();

	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	if (!IsValid(NavSys))
	{
		return EBTNodeResult::Failed;
	}

	// Tracked across every attempt (not just failed ones) so the fallback below has something useful to
	// offer even if no attempt actually lands in full shadow — the closest reachable point tested is a
	// better fallback than the pawn's own current (already-sunlit, by definition of why this task is
	// running at all) location.
	bool bFoundAnyNavPoint = false;
	float ClosestDistSq = TNumericLimits<float>::Max();
	FVector ClosestPoint = Origin;

	for (int32 Attempt = 0; Attempt < MaxTraceAttempts; ++Attempt)
	{
		FNavLocation RandomNavLocation;
		if (!NavSys->GetRandomReachablePointInRadius(Origin, SearchRadius, RandomNavLocation))
		{
			continue;
		}

		bFoundAnyNavPoint = true;
		const float DistSq = FVector::DistSquared(Origin, RandomNavLocation.Location);
		if (DistSq < ClosestDistSq)
		{
			ClosestDistSq = DistSq;
			ClosestPoint = RandomNavLocation.Location;
		}

		// +100 Z: approximates head height above the sampled NavMesh point (which sits at ground/floor
		// level) — same reasoning as ABaseMonster::CheckSunExposure()'s capsule-half-height offset. Tracing
		// from ground level itself would false-negative on totally flat, open ground with genuine overhead
		// cover a few units up.
		const FVector TraceStart = RandomNavLocation.Location + FVector(0.0f, 0.0f, 100.0f);
		const FVector TraceEnd = TraceStart + DirectionToSun * 10000.0f;

		FCollisionQueryParams QueryParams;
		QueryParams.AddIgnoredActor(ControlledPawn);

		FHitResult Hit;
		const bool bBlockedFromSun = World->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, TraceChannel, QueryParams);

		UE_LOG(LogTemp, Warning, TEXT("%s: attempt %d/%d at %s -> %s"), *GetNodeName(), Attempt + 1, MaxTraceAttempts,
			*RandomNavLocation.Location.ToCompactString(), bBlockedFromSun ? TEXT("SHADOW") : TEXT("sunlit"));

		if (bBlockedFromSun)
		{
			BlackboardComp->SetValueAsVector(TargetLocationKey.SelectedKeyName, RandomNavLocation.Location);

			UE_LOG(LogTemp, Warning, TEXT("%s: found shadow point at %s after %d attempt(s)"), *GetNodeName(),
				*RandomNavLocation.Location.ToCompactString(), Attempt + 1);
			if (GEngine)
			{
				GEngine->AddOnScreenDebugMessage(308, 3.0f, FColor::Green,
					FString::Printf(TEXT("%s: shadow point found (%d attempts)"), *ControlledPawn->GetName(), Attempt + 1));
			}

			return EBTNodeResult::Succeeded;
		}
	}

	// No attempt found a fully-shadowed point. Fall back to the closest reachable NavMesh point actually
	// tested (a "best effort" location still worth moving to, even though it wasn't verified shaded) if any
	// were found at all — Succeeded, since a usable destination WAS found and stored, just not a
	// guaranteed-shaded one, and a sibling MoveTo task can still act on it. Only fall back to this pawn's
	// OWN current location — Failed, nothing useful to offer at all — if the NavMesh search itself never
	// produced a single reachable point.
	if (bFoundAnyNavPoint)
	{
		BlackboardComp->SetValueAsVector(TargetLocationKey.SelectedKeyName, ClosestPoint);

		UE_LOG(LogTemp, Warning, TEXT("%s: no fully-shadowed point in %d attempts — falling back to closest tested point %s"),
			*GetNodeName(), MaxTraceAttempts, *ClosestPoint.ToCompactString());
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(309, 3.0f, FColor::Orange,
				FString::Printf(TEXT("%s: no full shadow found - using closest point"), *ControlledPawn->GetName()));
		}

		return EBTNodeResult::Succeeded;
	}

	BlackboardComp->SetValueAsVector(TargetLocationKey.SelectedKeyName, Origin);

	UE_LOG(LogTemp, Warning, TEXT("%s: NavMesh search produced no reachable point at all — falling back to current location"), *GetNodeName());
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(310, 3.0f, FColor::Red,
			FString::Printf(TEXT("%s: shadow search FAILED - no reachable NavMesh point"), *ControlledPawn->GetName()));
	}

	return EBTNodeResult::Failed;
}

FString UBTTask_FindShadowPoint::GetStaticDescription() const
{
	return FString::Printf(TEXT("Find Shadow Point: radius %.0f, %d attempts -> %s"),
		SearchRadius, MaxTraceAttempts, *TargetLocationKey.SelectedKeyName.ToString());
}

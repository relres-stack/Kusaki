// Fill out your copyright notice in the Description page of Project Settings.

#include "HeroScriptCommand.h"

#include "AIController.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "BaseCharacter.h"
#include "BaseMonster.h"
#include "Components/CapsuleComponent.h"
#include "Components/HeroScriptRunner.h"
#include "Components/ScriptedKnockdownComponent.h"
#include "Engine/Engine.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HeroScriptNavFilter.h"
#include "DrawDebugHelpers.h"
#include "Kismet/GameplayStatics.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationData.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"
#include "PrologueDirector.h"
#include "Sound/SoundBase.h"

namespace
{
	// A move that keeps failing (target off the navmesh, blocked) is given up after this many tries: the scenario
	// must go on, so the command ends with a warning instead of hanging the step.
	constexpr int32 MaxMoveAttempts = 3;
	// TurnTo gives up after this long (a target right on top of the hero, or a hero that cannot rotate).
	constexpr double MaxTurnSeconds = 4.0;
	// A move counts as stuck when the hero gets no closer than this, cm...
	constexpr float StuckProgressCm = 30.0f;
	// ...for this long, s (longer when following someone who walks too).
	constexpr double StuckSeconds = 3.0;
	constexpr double StuckSecondsFollowing = 6.0;
	// Riding (step 4.3b): how often the path for the ridden monster is computed again, s, and how close to a path point
	// counts as passed, cm.
	constexpr double RideRepathSeconds = 0.5;
	constexpr float RidePointReachedCm = 150.0f;
}

FString FHeroScriptTarget::Describe() const
{
	switch (Type)
	{
	case EHeroScriptTargetType::ActorWithTag:	return FString::Printf(TEXT("tag '%s'"), *Tag.ToString());
	case EHeroScriptTargetType::Hero:			return HeroId.IsNone() ? FString(TEXT("player's hero")) : FString::Printf(TEXT("hero %s"), *HeroId.ToString());
	case EHeroScriptTargetType::NearestMonster:	return TEXT("nearest monster");
	case EHeroScriptTargetType::Location:		return FString::Printf(TEXT("point %s"), *Location.ToCompactString());
	}
	return TEXT("?");
}

// ---------------------------------------------------------------------------------------------------------------
// Move to

void UHeroScriptCommand_MoveTo::Start(UHeroScriptRunner& Runner)
{
	ABaseCharacter* Hero = Runner.GetHero();
	UCharacterMovementComponent* MoveComp = Hero->GetCharacterMovement();

	// Riding a monster: no path following and no speed change of his own — the monster carries him (TickRiding()).
	bRiding = Hero->IsMounted() && IsValid(Hero->GetMountedMonster());
	if (bRiding)
	{
		BestDistance = MAX_FLT;
		ProgressTime = Runner.GetWorldTime();
		NextRepathTime = 0.0;
		UE_LOG(LogTemp, Warning, TEXT("Prologue: %s rides %s to %s"), *Runner.GetHeroName(), *Hero->GetMountedMonster()->GetName(), *Target.Describe());
		return;
	}

	switch (Speed)
	{
	case EHeroScriptMoveSpeed::Walk:
		if (Hero->bIsSprinting)
		{
			Hero->StopSprinting();
		}
		break;
	case EHeroScriptMoveSpeed::Run:
		if (!Hero->bIsSprinting)
		{
			Hero->StartSprinting();   // no-op while crouched, like for the player
			bStartedSprint = Hero->bIsSprinting;
		}
		break;
	case EHeroScriptMoveSpeed::Custom:
		if (Hero->bIsSprinting)
		{
			Hero->StopSprinting();
		}
		if (MoveComp)
		{
			SavedMaxWalkSpeed = MoveComp->MaxWalkSpeed;
			MoveComp->MaxWalkSpeed = CustomSpeed;
			bSpeedOverridden = true;
		}
		break;
	}

	if (LoopMontage)
	{
		Hero->PlayAnimMontage(LoopMontage);
	}

	// Step 4.8: he is turned by UpdateFacing(), so nothing else may turn him while he walks; put back in End().
	if (Facing != EHeroScriptMoveFacing::Forward && MoveComp)
	{
		bSavedOrientToMovement = MoveComp->bOrientRotationToMovement;
		bSavedUseControllerDesiredRotation = MoveComp->bUseControllerDesiredRotation;
		bSavedUseControllerRotationYaw = Hero->bUseControllerRotationYaw;
		SavedRotationRate = MoveComp->RotationRate;
		MoveComp->bOrientRotationToMovement = false;
		MoveComp->bUseControllerDesiredRotation = false;
		Hero->bUseControllerRotationYaw = false;
		// ABaseCharacter::Tick() turns "face where I move" back on every frame (it follows aiming): with no turn rate the
		// movement cannot turn him anyway. Found in PIE 27.11.2026 — without it he walked face first.
		MoveComp->RotationRate = FRotator::ZeroRotator;
		bFacingOverridden = true;
		// Until he moves, "the way he walks" is the straight line to the goal: he turns his back to it at once.
		AActor* GoalActor = nullptr;
		FVector GoalLocation;
		if (Runner.ResolveTarget(Target, GoalActor, GoalLocation))
		{
			MoveDirection = (GoalLocation - Hero->GetActorLocation()).GetSafeNormal2D();
		}
	}
	RequestMove(Runner);
}

bool UHeroScriptCommand_MoveTo::ComputeFacingYaw(EHeroScriptMoveFacing InFacing, const FVector& HeroLocation, const FVector& InMoveDirection,
	const FVector& TargetLocation, float& OutYaw)
{
	FVector Direction = FVector::ZeroVector;
	switch (InFacing)
	{
	case EHeroScriptMoveFacing::Backwards:
		Direction = -InMoveDirection;
		break;
	case EHeroScriptMoveFacing::Target:
		Direction = TargetLocation - HeroLocation;
		break;
	default:
		return false;
	}
	Direction.Z = 0.0f;
	if (!Direction.Normalize())
	{
		return false;
	}
	OutYaw = Direction.Rotation().Yaw;
	return true;
}

void UHeroScriptCommand_MoveTo::UpdateFacing(UHeroScriptRunner& Runner, float DeltaSeconds)
{
	ABaseCharacter* Hero = Runner.GetHero();
	if (!bFacingOverridden || !IsValid(Hero))
	{
		return;
	}
	// The way he really walks (path corners included); kept while he stands for a moment.
	const FVector Velocity = Hero->GetVelocity().GetSafeNormal2D();
	if (Hero->GetVelocity().SizeSquared2D() > FMath::Square(20.0f))
	{
		MoveDirection = Velocity;
	}
	FVector TargetLocation = FVector::ZeroVector;
	if (Facing == EHeroScriptMoveFacing::Target)
	{
		AActor* FaceActor = nullptr;
		if (!Runner.ResolveTarget(FaceTarget, FaceActor, TargetLocation))
		{
			return;
		}
	}
	float WantedYaw = 0.0f;
	if (!ComputeFacingYaw(Facing, Hero->GetActorLocation(), MoveDirection, TargetLocation, WantedYaw))
	{
		return;
	}
	const float Yaw = Hero->GetActorRotation().Yaw;
	const float Delta = FMath::FindDeltaAngleDegrees(Yaw, WantedYaw);
	const float MaxStep = FacingTurnSpeed * DeltaSeconds;
	Hero->SetActorRotation(FRotator(0.0f, Yaw + FMath::Clamp(Delta, -MaxStep, MaxStep), 0.0f));
}

void UHeroScriptCommand_MoveTo::RequestMove(UHeroScriptRunner& Runner)
{
	AAIController* AI = Runner.GetAIController();
	if (!IsValid(AI))
	{
		return;
	}

	AActor* GoalActor = nullptr;
	FVector GoalLocation;
	if (!Runner.ResolveTarget(Target, GoalActor, GoalLocation))
	{
		UE_LOG(LogTemp, Warning, TEXT("Prologue: %s cannot find %s - the move is skipped"), *Runner.GetHeroName(), *Target.Describe());
		bArrived = true;
		return;
	}
	if (GoalActor == Runner.GetHero())
	{
		bArrived = true;
		return;
	}

	UPathFollowingComponent* PathFollowing = AI->GetPathFollowingComponent();
	if (PathFollowing && !MoveFinishedHandle.IsValid())
	{
		MoveFinishedHandle = PathFollowing->OnRequestFinished.AddUObject(this, &UHeroScriptCommand_MoveTo::HandleMoveFinished);
		BoundPathFollowing = PathFollowing;
	}

	// Heroes and monsters move: follow them. Markers stand still: walk to the spot (projected onto the navmesh).
	const bool bFollowActor = IsValid(GoalActor) && GoalActor->IsA<APawn>();
	// UHeroScriptNavFilter: the light area of a candle/censer in the hero's own hand is plain floor for him,
	// otherwise he keeps routing around his own light and circles on the spot.
	const EPathFollowingRequestResult::Type Result = bFollowActor
		? AI->MoveToActor(GoalActor, AcceptanceRadius, true, !bWithoutPathfinding, false, UHeroScriptNavFilter::StaticClass())
		: AI->MoveToLocation(GoalLocation, AcceptanceRadius, true, !bWithoutPathfinding, true, false, UHeroScriptNavFilter::StaticClass());

	switch (Result)
	{
	case EPathFollowingRequestResult::AlreadyAtGoal:
		bArrived = true;
		break;
	case EPathFollowingRequestResult::RequestSuccessful:
		bMoveInFlight = true;
		RequestId = AI->GetCurrentMoveRequestID().GetID();
		BestDistance = MAX_FLT;
		ProgressTime = Runner.GetWorldTime();
		break;
	default:
		++Attempts;
		if (!bWithoutPathfinding)
		{
			// No navmesh path (test level without a navmesh, target off it): walk straight instead of standing.
			bWithoutPathfinding = true;
			RetryTime = Runner.GetWorldTime();
			UE_LOG(LogTemp, Warning, TEXT("Prologue: %s finds no navmesh path to %s - walks straight"), *Runner.GetHeroName(), *Target.Describe());
		}
		else
		{
			RetryTime = Runner.GetWorldTime() + 1.0;
		}
		break;
	}
}

void UHeroScriptCommand_MoveTo::HandleMoveFinished(FAIRequestID FinishedId, const FPathFollowingResult& Result)
{
	if (!bMoveInFlight || FinishedId.GetID() != RequestId)
	{
		return;
	}
	bMoveInFlight = false;
	if (Result.IsSuccess())
	{
		bArrived = true;
		return;
	}
	++Attempts;
	const UWorld* World = GetWorld();
	RetryTime = (World ? World->GetTimeSeconds() : 0.0) + 0.5;
	UE_LOG(LogTemp, Log, TEXT("Prologue: hero move to %s ended without arriving (%s), try %d/%d"),
		*Target.Describe(), *Result.ToString(), Attempts, MaxMoveAttempts);
}

EHeroScriptCommandStatus UHeroScriptCommand_MoveTo::Tick(UHeroScriptRunner& Runner, float DeltaSeconds)
{
	if (bArrived)
	{
		return EHeroScriptCommandStatus::Done;
	}
	if (bRiding)
	{
		return TickRiding(Runner);
	}
	UpdateFacing(Runner, DeltaSeconds);

	// Stuck: moving (maybe even fast, circling under a path point he cannot reach) but no closer to the target.
	const double Now = Runner.GetWorldTime();
	AActor* GoalActor = nullptr;
	FVector GoalLocation;
	if (bMoveInFlight && Runner.ResolveTarget(Target, GoalActor, GoalLocation))
	{
		const float Distance = FVector::Dist2D(Runner.GetHero()->GetActorLocation(), GoalLocation);
		if (Distance < BestDistance - StuckProgressCm)
		{
			BestDistance = Distance;
			ProgressTime = Now;
		}
		const double Window = (IsValid(GoalActor) && GoalActor->IsA<APawn>()) ? StuckSecondsFollowing : StuckSeconds;
		if (Now - ProgressTime > Window)
		{
			++Attempts;
			UE_LOG(LogTemp, Warning, TEXT("Prologue: %s got no closer to %s for %.0f s (%.1f m left) - new path, try %d/%d"),
				*Runner.GetHeroName(), *Target.Describe(), Window, Distance / 100.0f, Attempts, MaxMoveAttempts);
			bMoveInFlight = false;   // the old request's "aborted" callback is ignored from here on
			if (AAIController* AI = Runner.GetAIController())
			{
				AI->StopMovement();
			}
			RetryTime = Now;
		}
	}

	if (Attempts >= MaxMoveAttempts)
	{
		GiveUp(Runner);
		return EHeroScriptCommandStatus::Done;
	}
	if (!bMoveInFlight && Now >= RetryTime)
	{
		RequestMove(Runner);
	}

	if (LoopMontage)
	{
		ABaseCharacter* Hero = Runner.GetHero();
		const UAnimInstance* Anim = Hero->GetMesh() ? Hero->GetMesh()->GetAnimInstance() : nullptr;
		if (Anim && !Anim->Montage_IsPlaying(LoopMontage))
		{
			Hero->PlayAnimMontage(LoopMontage);
		}
	}
	return bArrived ? EHeroScriptCommandStatus::Done : EHeroScriptCommandStatus::Running;
}

EHeroScriptCommandStatus UHeroScriptCommand_MoveTo::TickRiding(UHeroScriptRunner& Runner)
{
	ABaseCharacter* Hero = Runner.GetHero();
	ABaseMonster* Mount = Hero->GetMountedMonster();
	if (!Hero->IsMounted() || !IsValid(Mount))
	{
		// Knocked off or got down on the way: the rest of the way on foot.
		bRiding = false;
		UE_LOG(LogTemp, Warning, TEXT("Prologue: %s is no longer riding - goes on to %s on foot"), *Runner.GetHeroName(), *Target.Describe());
		RequestMove(Runner);
		return EHeroScriptCommandStatus::Running;
	}

	AActor* GoalActor = nullptr;
	FVector GoalLocation;
	if (!Runner.ResolveTarget(Target, GoalActor, GoalLocation))
	{
		UE_LOG(LogTemp, Warning, TEXT("Prologue: %s cannot find %s - the ride is skipped"), *Runner.GetHeroName(), *Target.Describe());
		return EHeroScriptCommandStatus::Done;
	}

	const FVector From = Mount->GetActorLocation();
	const float MountRadius = Mount->GetCapsuleComponent() ? Mount->GetCapsuleComponent()->GetScaledCapsuleRadius() : 50.0f;
	const float Distance = FVector::Dist2D(From, GoalLocation);
	if (Distance <= AcceptanceRadius + MountRadius)
	{
		bArrived = true;
		return EHeroScriptCommandStatus::Done;
	}

	const double Now = Runner.GetWorldTime();
	UWorld* World = Hero->GetWorld();
	if (RidePath.Num() == 0 || Now >= NextRepathTime)
	{
		NextRepathTime = Now + RideRepathSeconds;
		RidePath.Reset();
		RidePathIndex = 0;
		// From where the monster stands, on the level's main navmesh: with the monster as the query context no path was
		// found at all (24.11.2026, "rides straight" - its nav agent is sized by its capsule, NavAgentProps -1, and
		// probably matches no navmesh), so no context, like the heroes' own default agent.
		UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		const UNavigationPath* Path = Nav ? Nav->FindPathToLocationSynchronously(World, From, GoalLocation, nullptr) : nullptr;
		if (Path && Path->IsValid() && Path->PathPoints.Num() > 0)
		{
			RidePath = Path->PathPoints;
			bWithoutPathfinding = false;
		}
		else
		{
			if (!bWithoutPathfinding)
			{
				UE_LOG(LogTemp, Warning, TEXT("Prologue: %s finds no navmesh path for the ridden monster to %s - rides straight"), *Runner.GetHeroName(), *Target.Describe());
			}
			RidePath.Add(GoalLocation);
			bWithoutPathfinding = true;
		}
	}
	while (RidePathIndex < RidePath.Num() - 1 && FVector::Dist2D(From, RidePath[RidePathIndex]) < RidePointReachedCm)
	{
		++RidePathIndex;
	}
	// Handed to the monster by ABaseCharacter::AddMovementInput(), as the player's WASD is (bForce: a scene's blocked
	// input does not stop a scripted ride).
	Hero->AddMovementInput((RidePath[RidePathIndex] - From).GetSafeNormal2D(), 1.0f, true);

	if (Distance < BestDistance - StuckProgressCm)
	{
		BestDistance = Distance;
		ProgressTime = Now;
	}
	else if (Now - ProgressTime > StuckSeconds)
	{
		++Attempts;
		ProgressTime = Now;
		NextRepathTime = Now;
		UE_LOG(LogTemp, Warning, TEXT("Prologue: %s's monster got no closer to %s for %.0f s (%.1f m left) - new path, try %d/%d"),
			*Runner.GetHeroName(), *Target.Describe(), StuckSeconds, Distance / 100.0f, Attempts, MaxMoveAttempts);
		if (Attempts >= MaxMoveAttempts)
		{
			RideGiveUp(Runner, Mount, GoalLocation);
			return EHeroScriptCommandStatus::Done;
		}
	}
	return EHeroScriptCommandStatus::Running;
}

void UHeroScriptCommand_MoveTo::RideGiveUp(UHeroScriptRunner& Runner, ABaseMonster* Mount, const FVector& GoalLocation)
{
	bArrived = true;
	if (!bTeleportIfStuck)
	{
		UE_LOG(LogTemp, Warning, TEXT("Prologue: %s could not ride to %s after %d tries - stays where he is, the script goes on"),
			*Runner.GetHeroName(), *Target.Describe(), Attempts);
		return;
	}
	// The monster is put there (the rider is attached to it and comes along), on the navmesh if there is one.
	FVector Destination = GoalLocation;
	const float HalfHeight = Mount->GetCapsuleComponent() ? Mount->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 90.0f;
	FNavLocation OnNavmesh;
	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(Mount->GetWorld());
	Destination = (Nav && Nav->ProjectPointToNavigation(GoalLocation, OnNavmesh, FVector(200.0f, 200.0f, 400.0f)))
		? OnNavmesh.Location + FVector(0.0f, 0.0f, HalfHeight)
		: GoalLocation + FVector(0.0f, 0.0f, HalfHeight);
	const FRotator PutFacing(0.0f, (GoalLocation - Mount->GetActorLocation()).GetSafeNormal2D().Rotation().Yaw, 0.0f);
	if (!Mount->TeleportTo(Destination, PutFacing))
	{
		Mount->SetActorLocationAndRotation(Destination, PutFacing, false, nullptr, ETeleportType::TeleportPhysics);
	}
	const FString Message = FString::Printf(TEXT("Prologue: %s's monster got stuck on the way to %s and was put there (fix the navmesh/level at %s)"),
		*Runner.GetHeroName(), *Target.Describe(), *Mount->GetActorLocation().ToCompactString());
	UE_LOG(LogTemp, Warning, TEXT("%s"), *Message);
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 8.0f, FColor::Orange, Message);
	}
}

void UHeroScriptCommand_MoveTo::GiveUp(UHeroScriptRunner& Runner)
{
	ABaseCharacter* Hero = Runner.GetHero();
	AActor* GoalActor = nullptr;
	FVector GoalLocation;
	if (!bTeleportIfStuck || !Runner.ResolveTarget(Target, GoalActor, GoalLocation))
	{
		UE_LOG(LogTemp, Warning, TEXT("Prologue: %s could not get to %s after %d tries - stays where he is, the script goes on"),
			*Runner.GetHeroName(), *Target.Describe(), Attempts);
		return;
	}

	// Next to a hero/monster (not inside him), on the spot itself for a marker; snapped to the navmesh if there is one.
	FVector Destination = GoalLocation;
	if (IsValid(GoalActor) && GoalActor->IsA<APawn>())
	{
		const FVector Away = (Hero->GetActorLocation() - GoalLocation).GetSafeNormal2D();
		Destination = GoalLocation + Away * (AcceptanceRadius + 100.0f);
	}
	const float HalfHeight = Hero->GetCapsuleComponent() ? Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 90.0f;
	FNavLocation OnNavmesh;
	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(Hero->GetWorld());
	if (Nav && Nav->ProjectPointToNavigation(Destination, OnNavmesh, FVector(200.0f, 200.0f, 400.0f)))
	{
		Destination = OnNavmesh.Location + FVector(0.0f, 0.0f, HalfHeight);
	}
	else if (!(IsValid(GoalActor) && GoalActor->IsA<APawn>()))
	{
		Destination.Z += HalfHeight;   // markers and locations mark the ground
	}

	if (AAIController* AI = Runner.GetAIController())
	{
		AI->StopMovement();
	}
	const FRotator PutFacing(0.0f, (GoalLocation - Destination).GetSafeNormal2D().Rotation().Yaw, 0.0f);
	if (!Hero->TeleportTo(Destination, PutFacing))
	{
		Hero->SetActorLocationAndRotation(Destination, PutFacing, false, nullptr, ETeleportType::TeleportPhysics);
	}
	bArrived = true;

	const FString Message = FString::Printf(TEXT("Prologue: %s got stuck on the way to %s and was put there (fix the navmesh/level at %s)"),
		*Runner.GetHeroName(), *Target.Describe(), *Hero->GetActorLocation().ToCompactString());
	UE_LOG(LogTemp, Warning, TEXT("%s"), *Message);
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 8.0f, FColor::Orange, Message);
	}
}

void UHeroScriptCommand_MoveTo::DrawDebug(const UHeroScriptRunner& Runner) const
{
	if (bRiding)
	{
		// The ridden monster's path: magenta, the point it heads for now orange.
		if (UWorld* RideWorld = Runner.GetWorld())
		{
			const FVector Lift(0.0f, 0.0f, 30.0f);
			for (int32 i = 0; i < RidePath.Num(); ++i)
			{
				const bool bNext = i == RidePathIndex;
				DrawDebugSphere(RideWorld, RidePath[i] + Lift, bNext ? 30.0f : 15.0f, 8, bNext ? FColor::Orange : FColor::Magenta, false, -1.0f, 0, 2.0f);
				if (i > 0)
				{
					DrawDebugLine(RideWorld, RidePath[i - 1] + Lift, RidePath[i] + Lift, FColor::Magenta, false, -1.0f, 0, 4.0f);
				}
			}
		}
		return;
	}
	const AAIController* AI = Runner.GetAIController();
	const UPathFollowingComponent* PathFollowing = IsValid(AI) ? AI->GetPathFollowingComponent() : nullptr;
	UWorld* World = Runner.GetWorld();
	if (!PathFollowing || !World || !PathFollowing->GetPath().IsValid())
	{
		return;
	}
	// The path the hero follows: cyan, the point he is heading for now orange.
	const TArray<FNavPathPoint>& Points = PathFollowing->GetPath()->GetPathPoints();
	const FVector Lift(0.0f, 0.0f, 30.0f);
	for (int32 i = 0; i < Points.Num(); ++i)
	{
		const bool bNext = i == static_cast<int32>(PathFollowing->GetNextPathIndex());
		DrawDebugSphere(World, Points[i].Location + Lift, bNext ? 30.0f : 15.0f, 8, bNext ? FColor::Orange : FColor::Cyan, false, -1.0f, 0, 2.0f);
		if (i > 0)
		{
			DrawDebugLine(World, Points[i - 1].Location + Lift, Points[i].Location + Lift, FColor::Cyan, false, -1.0f, 0, 4.0f);
		}
	}
}

void UHeroScriptCommand_MoveTo::End(UHeroScriptRunner& Runner, bool bInterrupted)
{
	if (BoundPathFollowing.IsValid() && MoveFinishedHandle.IsValid())
	{
		BoundPathFollowing->OnRequestFinished.Remove(MoveFinishedHandle);
	}
	MoveFinishedHandle.Reset();

	if (bMoveInFlight)
	{
		if (AAIController* AI = Runner.GetAIController())
		{
			AI->StopMovement();
		}
		bMoveInFlight = false;
	}

	ABaseCharacter* Hero = Runner.GetHero();
	if (!IsValid(Hero))
	{
		return;
	}
	if (bStartedSprint && Hero->bIsSprinting)
	{
		Hero->StopSprinting();
	}
	// Put the speed back only if nothing else changed it meanwhile (a trap or panic keeps its own saved value).
	UCharacterMovementComponent* MoveComp = Hero->GetCharacterMovement();
	if (bSpeedOverridden && MoveComp && FMath::IsNearlyEqual(MoveComp->MaxWalkSpeed, CustomSpeed))
	{
		MoveComp->MaxWalkSpeed = SavedMaxWalkSpeed;
	}
	if (LoopMontage)
	{
		Hero->StopAnimMontage(LoopMontage);
	}
	if (bFacingOverridden && MoveComp)
	{
		MoveComp->bOrientRotationToMovement = bSavedOrientToMovement;
		MoveComp->bUseControllerDesiredRotation = bSavedUseControllerDesiredRotation;
		Hero->bUseControllerRotationYaw = bSavedUseControllerRotationYaw;
		if (MoveComp->RotationRate.IsZero())
		{
			MoveComp->RotationRate = SavedRotationRate;
		}
		bFacingOverridden = false;
	}
}

FString UHeroScriptCommand_MoveTo::Describe() const
{
	const TCHAR* How = Speed == EHeroScriptMoveSpeed::Run ? TEXT("run") : Speed == EHeroScriptMoveSpeed::Custom ? TEXT("crawl") : TEXT("walk");
	const FString Way = Facing == EHeroScriptMoveFacing::Backwards ? TEXT(", backwards")
		: Facing == EHeroScriptMoveFacing::Target ? FString::Printf(TEXT(", facing %s"), *FaceTarget.Describe()) : FString();
	return Speed == EHeroScriptMoveSpeed::Custom
		? FString::Printf(TEXT("%s to %s at %.0f%s"), How, *Target.Describe(), CustomSpeed, *Way)
		: FString::Printf(TEXT("%s to %s%s"), How, *Target.Describe(), *Way);
}

FString UHeroScriptCommand_MoveTo::DescribeProgress(const UHeroScriptRunner& Runner) const
{
	const ABaseCharacter* Hero = Runner.GetHero();
	AActor* GoalActor = nullptr;
	FVector GoalLocation;
	if (!IsValid(Hero) || !Runner.ResolveTarget(Target, GoalActor, GoalLocation))
	{
		return FString();
	}
	const AActor* Mover = (bRiding && IsValid(Hero->GetMountedMonster())) ? static_cast<const AActor*>(Hero->GetMountedMonster()) : Hero;
	return FString::Printf(TEXT("%s%.1f m left, %.0f cm/s%s, try %d"), bRiding ? TEXT("riding, ") : TEXT(""),
		FVector::Dist2D(Mover->GetActorLocation(), GoalLocation) / 100.0f,
		Mover->GetVelocity().Size2D(), bWithoutPathfinding ? TEXT(", no navmesh path") : TEXT(""), Attempts + 1);
}

// ---------------------------------------------------------------------------------------------------------------
// Turn to

void UHeroScriptCommand_TurnTo::Start(UHeroScriptRunner& Runner)
{
	StartTime = Runner.GetWorldTime();
	if (AAIController* AI = Runner.GetAIController())
	{
		AI->StopMovement();
	}
}

EHeroScriptCommandStatus UHeroScriptCommand_TurnTo::Tick(UHeroScriptRunner& Runner, float DeltaSeconds)
{
	ABaseCharacter* Hero = Runner.GetHero();
	AActor* GoalActor = nullptr;
	FVector GoalLocation;
	if (!Runner.ResolveTarget(Target, GoalActor, GoalLocation))
	{
		UE_LOG(LogTemp, Warning, TEXT("Prologue: %s cannot find %s to turn to"), *Runner.GetHeroName(), *Target.Describe());
		return EHeroScriptCommandStatus::Done;
	}

	FVector ToGoal = GoalLocation - Hero->GetActorLocation();
	ToGoal.Z = 0.0;
	if (ToGoal.IsNearlyZero())
	{
		return EHeroScriptCommandStatus::Done;
	}

	FRotator Rotation = Hero->GetActorRotation();
	const float GoalYaw = ToGoal.Rotation().Yaw;
	const float Delta = FMath::FindDeltaAngleDegrees(Rotation.Yaw, GoalYaw);
	const float Step = TurnSpeed * DeltaSeconds;
	if (FMath::Abs(Delta) <= FMath::Max(Step, 2.0f))
	{
		Rotation.Yaw = GoalYaw;
		Hero->SetActorRotation(Rotation);
		return EHeroScriptCommandStatus::Done;
	}
	Rotation.Yaw += FMath::Sign(Delta) * Step;
	Hero->SetActorRotation(Rotation);
	return Runner.GetWorldTime() - StartTime > MaxTurnSeconds ? EHeroScriptCommandStatus::Done : EHeroScriptCommandStatus::Running;
}

FString UHeroScriptCommand_TurnTo::Describe() const
{
	return FString::Printf(TEXT("turn to %s"), *Target.Describe());
}

// ---------------------------------------------------------------------------------------------------------------
// Wait

void UHeroScriptCommand_Wait::Start(UHeroScriptRunner& Runner)
{
	StartTime = Runner.GetWorldTime();
}

EHeroScriptCommandStatus UHeroScriptCommand_Wait::Tick(UHeroScriptRunner& Runner, float DeltaSeconds)
{
	return Runner.GetWorldTime() - StartTime >= Seconds ? EHeroScriptCommandStatus::Done : EHeroScriptCommandStatus::Running;
}

FString UHeroScriptCommand_Wait::Describe() const
{
	return FString::Printf(TEXT("wait %.1f s"), Seconds);
}

// ---------------------------------------------------------------------------------------------------------------
// Play montage

void UHeroScriptCommand_PlayMontage::Start(UHeroScriptRunner& Runner)
{
	if (!Montage)
	{
		UE_LOG(LogTemp, Warning, TEXT("Prologue: %s has a 'Play montage' command without a montage"), *Runner.GetHeroName());
		return;
	}
	bPlaying = Runner.GetHero()->PlayAnimMontage(Montage, PlayRate) > 0.0f;
	if (!bPlaying)
	{
		UE_LOG(LogTemp, Warning, TEXT("Prologue: %s could not play %s (wrong skeleton or no AnimBP slot?)"), *Runner.GetHeroName(), *GetNameSafe(Montage));
	}
}

EHeroScriptCommandStatus UHeroScriptCommand_PlayMontage::Tick(UHeroScriptRunner& Runner, float DeltaSeconds)
{
	if (!bPlaying || !bWaitForEnd)
	{
		return EHeroScriptCommandStatus::Done;
	}
	const ABaseCharacter* Hero = Runner.GetHero();
	const UAnimInstance* Anim = Hero->GetMesh() ? Hero->GetMesh()->GetAnimInstance() : nullptr;
	return Anim && Anim->Montage_IsPlaying(Montage) ? EHeroScriptCommandStatus::Running : EHeroScriptCommandStatus::Done;
}

void UHeroScriptCommand_PlayMontage::End(UHeroScriptRunner& Runner, bool bInterrupted)
{
	// A montage that was meant to play on top of the next commands (bWaitForEnd off) is left alone.
	if (bInterrupted && bPlaying && bWaitForEnd && IsValid(Runner.GetHero()))
	{
		Runner.GetHero()->StopAnimMontage(Montage);
	}
}

FString UHeroScriptCommand_PlayMontage::Describe() const
{
	return FString::Printf(TEXT("montage %s"), *GetNameSafe(Montage));
}

// ---------------------------------------------------------------------------------------------------------------
// Say a line

void UHeroScriptCommand_Say::Start(UHeroScriptRunner& Runner)
{
	StartTime = Runner.GetWorldTime();
	const FString Line = FString::Printf(TEXT("%s: %s"), *Runner.GetHeroName(), *Text.ToString());
	UE_LOG(LogTemp, Warning, TEXT("Prologue line - %s"), *Line);
	if (GEngine)
	{
		// One key per hero: his next line replaces the previous one, two heroes' lines can be up at once.
		GEngine->AddOnScreenDebugMessage(Runner.GetSubtitleKey(), Duration, FColor(255, 230, 170), Line);
	}
	if (Sound)
	{
		UGameplayStatics::PlaySoundAtLocation(Runner.GetHero(), Sound, Runner.GetHero()->GetActorLocation());
	}
}

EHeroScriptCommandStatus UHeroScriptCommand_Say::Tick(UHeroScriptRunner& Runner, float DeltaSeconds)
{
	return !bWaitUntilDone || Runner.GetWorldTime() - StartTime >= Duration ? EHeroScriptCommandStatus::Done : EHeroScriptCommandStatus::Running;
}

FString UHeroScriptCommand_Say::Describe() const
{
	return FString::Printf(TEXT("say \"%s\""), *Text.ToString());
}

// ---------------------------------------------------------------------------------------------------------------
// Teleport

void UHeroScriptCommand_Teleport::Start(UHeroScriptRunner& Runner)
{
	ABaseCharacter* Hero = Runner.GetHero();
	AActor* GoalActor = nullptr;
	FVector Destination;
	if (!Runner.ResolveTarget(Target, GoalActor, Destination) || GoalActor == Hero)
	{
		UE_LOG(LogTemp, Warning, TEXT("Prologue: %s cannot find %s to teleport to"), *Runner.GetHeroName(), *Target.Describe());
		return;
	}

	const bool bTargetIsGround = Target.Type == EHeroScriptTargetType::ActorWithTag || Target.Type == EHeroScriptTargetType::Location;
	if (bFeetOnTarget && bTargetIsGround && Hero->GetCapsuleComponent())
	{
		Destination.Z += Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	}
	FRotator Rotation = Hero->GetActorRotation();
	if (bTakeTargetRotation && IsValid(GoalActor))
	{
		Rotation = FRotator(0.0f, GoalActor->GetActorRotation().Yaw, 0.0f);
	}

	if (AAIController* AI = Runner.GetAIController())
	{
		AI->StopMovement();
	}
	// TeleportTo nudges the hero out of walls/other pawns; if there is no room at all, put him there anyway.
	if (!Hero->TeleportTo(Destination, Rotation))
	{
		UE_LOG(LogTemp, Warning, TEXT("Prologue: %s does not fit at %s - placed there anyway"), *Runner.GetHeroName(), *Target.Describe());
		Hero->SetActorLocationAndRotation(Destination, Rotation, false, nullptr, ETeleportType::TeleportPhysics);
	}
}

FString UHeroScriptCommand_Teleport::Describe() const
{
	return FString::Printf(TEXT("teleport to %s"), *Target.Describe());
}

// ---------------------------------------------------------------------------------------------------------------
// Hide / show

void UHeroScriptCommand_Hide::Start(UHeroScriptRunner& Runner)
{
	Runner.SetHeroHidden(bHide);
}

FString UHeroScriptCommand_Hide::Describe() const
{
	return bHide ? TEXT("hide") : TEXT("show again");
}

// ---------------------------------------------------------------------------------------------------------------
// Use ability (call function)

void UHeroScriptCommand_CallFunction::Start(UHeroScriptRunner& Runner)
{
	ABaseCharacter* Hero = Runner.GetHero();
	UObject* Target = Hero;
	if (Component)
	{
		Target = Hero->FindComponentByClass(Component);
		if (!IsValid(Target))
		{
			UE_LOG(LogTemp, Warning, TEXT("Prologue: %s has no %s - cannot call %s"), *Runner.GetHeroName(), *Component->GetName(), *FunctionName.ToString());
			return;
		}
	}

	UFunction* Function = FunctionName.IsNone() ? nullptr : Target->FindFunction(FunctionName);
	if (!Function)
	{
		UE_LOG(LogTemp, Warning, TEXT("Prologue: %s has no function '%s'"), *Target->GetClass()->GetName(), *FunctionName.ToString());
		return;
	}
	// Only functions without parameters: there is nothing in the asset to fill them with. A return value is fine.
	for (TFieldIterator<FProperty> It(Function); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
	{
		if (!It->HasAnyPropertyFlags(CPF_ReturnParm))
		{
			UE_LOG(LogTemp, Warning, TEXT("Prologue: %s::%s takes parameters - only functions without parameters can be called from the scenario"),
				*Target->GetClass()->GetName(), *FunctionName.ToString());
			return;
		}
	}

	uint8* Params = Function->ParmsSize > 0 ? static_cast<uint8*>(FMemory_Alloca(Function->ParmsSize)) : nullptr;
	if (Params)
	{
		FMemory::Memzero(Params, Function->ParmsSize);
		for (TFieldIterator<FProperty> It(Function); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
		{
			It->InitializeValue_InContainer(Params);
		}
	}
	Target->ProcessEvent(Function, Params);
	if (Params)
	{
		for (TFieldIterator<FProperty> It(Function); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
		{
			It->DestroyValue_InContainer(Params);
		}
	}
}

FString UHeroScriptCommand_CallFunction::Describe() const
{
	return Component
		? FString::Printf(TEXT("use %s.%s"), *Component->GetName(), *FunctionName.ToString())
		: FString::Printf(TEXT("use %s"), *FunctionName.ToString());
}

void UHeroScriptCommand_KnockDown::Start(UHeroScriptRunner& Runner)
{
	ABaseCharacter* Hero = Runner.GetHero();
	APrologueDirector* Director = Runner.GetDirector();
	UScriptedKnockdownComponent* Knockdown = IsValid(Director) ? Director->GetOrAddKnockdown(Hero) : nullptr;
	if (!IsValid(Knockdown))
	{
		UE_LOG(LogTemp, Warning, TEXT("Prologue: %s cannot fall or get up - no director"), *Runner.GetHeroName());
		return;
	}
	if (bGetUp)
	{
		bStarted = Knockdown->GetUp(Animation);
		return;
	}
	bStarted = Knockdown->KnockDown(Animation, bUntouchableWhileDown);
	FallEndTime = Runner.GetWorldTime() + Knockdown->GetFallDuration();
}

EHeroScriptCommandStatus UHeroScriptCommand_KnockDown::Tick(UHeroScriptRunner& Runner, float DeltaSeconds)
{
	if (!bStarted || !bWaitForAnimation)
	{
		return EHeroScriptCommandStatus::Done;
	}
	if (bGetUp)
	{
		const ABaseCharacter* Hero = Runner.GetHero();
		const UScriptedKnockdownComponent* Knockdown = IsValid(Hero) ? Hero->FindComponentByClass<UScriptedKnockdownComponent>() : nullptr;
		return IsValid(Knockdown) && Knockdown->IsDown() ? EHeroScriptCommandStatus::Running : EHeroScriptCommandStatus::Done;
	}
	return Runner.GetWorldTime() < FallEndTime ? EHeroScriptCommandStatus::Running : EHeroScriptCommandStatus::Done;
}

FString UHeroScriptCommand_KnockDown::Describe() const
{
	return FString::Printf(TEXT("%s%s"), bGetUp ? TEXT("get up") : TEXT("fall down"),
		Animation ? *FString::Printf(TEXT(" (%s)"), *Animation->GetName()) : TEXT(""));
}

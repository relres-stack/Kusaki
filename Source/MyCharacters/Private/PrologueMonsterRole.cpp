// Fill out your copyright notice in the Description page of Project Settings.

#include "PrologueMonsterRole.h"

#include "AIController.h"
#include "BaseCharacter.h"
#include "BaseMonster.h"
#include "Components/CapsuleComponent.h"
#include "Components/SphereComponent.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HolyLightZone.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationSystem.h"
#include "PrologueDirector.h"

namespace PrologueMonsterRole
{
	// A new move is sent only when the goal has shifted this far (cm) or the monster stopped — otherwise the
	// pressing ring would re-path every frame.
	constexpr float RepathDistance = 60.0f;
	// And not more often than this (s), so a goal it cannot reach is not asked for every frame either.
	constexpr double MoveRequestInterval = 0.25;

	// Goal moved out of every AHolyLightZone placed in the level (it burns any monster inside it, and the monster's
	// own AI does not avoid it either). Found in PIE 23.11.2026: "Run to hero" toward a Priest standing at the edge
	// of a zone walked the whole pack into it. Only the goal is moved — a path that crosses a zone is a matter of
	// where the level puts it.
	FVector KeepOutOfHolyLightZones(UWorld* World, const FVector& Goal, const ABaseMonster& Monster)
	{
		FVector Result = Goal;
		if (!World)
		{
			return Result;
		}
		const UCapsuleComponent* Capsule = Monster.GetCapsuleComponent();
		const float Margin = (Capsule ? Capsule->GetScaledCapsuleRadius() : 40.0f) + 50.0f;
		for (TActorIterator<AHolyLightZone> It(World); It; ++It)
		{
			const AHolyLightZone* Zone = *It;
			if (!IsValid(Zone) || !IsValid(Zone->LightZoneTrigger))
			{
				continue;
			}
			const FVector Center = Zone->LightZoneTrigger->GetComponentLocation();
			const float Keep = Zone->LightZoneTrigger->GetScaledSphereRadius() + Margin;
			FVector Offset = Result - Center;
			Offset.Z = 0.0f;
			if (Offset.SizeSquared() >= FMath::Square(Keep))
			{
				continue;
			}
			FVector Direction = Offset.GetSafeNormal();
			if (Direction.IsNearlyZero())
			{
				Direction = (Monster.GetActorLocation() - Center).GetSafeNormal2D();
			}
			if (Direction.IsNearlyZero())
			{
				Direction = FVector::ForwardVector;
			}
			Result = Center + Direction * Keep;
			Result.Z = Goal.Z;
		}
		return Result;
	}

	// The goal snapped onto the monster's navmesh. RequestMoveToLocation() does not project by default, and a goal
	// is built at the hero's height: with the hero on a platform, a point past its edge hangs in the air above the
	// ground and the move fails every time (found in PIE 23.11.2026: "driven off" monsters stood still). The tall
	// box finds the ground below such a point. False = no navmesh near it.
	bool ProjectToNavigation(const ABaseMonster& Monster, const FVector& Point, FVector& OutPoint)
	{
		UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(Monster.GetWorld());
		if (!Nav)
		{
			OutPoint = Point;
			return true;
		}
		const ANavigationData* NavData = Nav->GetNavDataForProps(Monster.GetNavAgentPropertiesRef(), Monster.GetActorLocation());
		FNavLocation Projected;
		if (Nav->ProjectPointToNavigation(Point, Projected, FVector(100.0f, 100.0f, 800.0f), NavData))
		{
			OutPoint = Projected.Location;
			return true;
		}
		return false;
	}
}

FString UPrologueMonsterRole::HeroName() const
{
	return HeroId.IsNone() ? FString(TEXT("player's hero")) : HeroId.ToString();
}

void UPrologueMonsterRole::TickMember(const FPrologueRoleContext& Context, FPrologueRoleMemberState& State) const
{
	ABaseMonster* Monster = Context.Monster;
	if (!IsValid(Monster) || !Context.Director)
	{
		return;
	}

	// "Driven off": a hit by light counts once its stun is over — then the monster backs off for a while.
	const bool bHit = Monster->IsStunned() || Monster->bIsStaggered;
	if (bRetreatWhenHit)
	{
		if (bHit)
		{
			State.bRetreatPending = true;
		}
		else if (State.bRetreatPending)
		{
			State.bRetreatPending = false;
			State.RetreatUntil = RetreatDuration > 0.0f ? Context.Time + RetreatDuration : TNumericLimits<double>::Max();
			State.bHasGoal = false;
		}
	}

	// The role's own setup holds even while the monster is stunned or hiding (e.g. "Sit on hero" collision).
	if (IsValid(Context.Hero) && !Context.Hero->bIsDead)
	{
		UpdateMember(Context, State);
	}

	// Its own reactions come first: a stun or a knockback (a candle, a censer swing) and the run from the sun. A move
	// sent now would cancel the stun's stop or drag it back into the sun.
	if (bHit || Monster->GetIsFleeingToShadow())
	{
		State.bHasGoal = false;
		return;
	}

	AAIController* AI = Cast<AAIController>(Monster->GetController());
	if (!AI)
	{
		return;
	}

	if (UCharacterMovementComponent* Move = Monster->GetCharacterMovement())
	{
		Move->MaxWalkSpeed = bRun ? Monster->ChaseSpeed : Monster->PatrolSpeed;
	}

	ABaseCharacter* Hero = Context.Hero;
	if (!IsValid(Hero) || Hero->bIsDead)
	{
		if (State.bHasGoal)
		{
			AI->StopMovement();
			State.bHasGoal = false;
		}
		return;
	}

	FVector Goal = FVector::ZeroVector;
	float Acceptance = 50.0f;
	const bool bRetreating = bRetreatWhenHit && State.IsRetreating(Context.Time);
	bool bHasGoal = false;
	if (bRetreating)
	{
		// Backs off along the line it stands on, still facing the hero.
		FVector Away = Monster->GetActorLocation() - Hero->GetActorLocation();
		Away.Z = 0.0f;
		if (!Away.Normalize())
		{
			Away = FRotator(0.0f, State.SlotBaseYaw + 360.0f * State.SlotIndex / FMath::Max(1, State.SlotCount), 0.0f).Vector();
		}
		Goal = Hero->GetActorLocation() + Away * RetreatDistance;
		Acceptance = 80.0f;
		bHasGoal = true;
	}
	else
	{
		bHasGoal = GetGoal(Context, State, Goal, Acceptance);
	}

	if (bHasGoal)
	{
		if (bKeepOutOfLight)
		{
			Goal = Context.Director->KeepOutOfHeroLight(Goal, *Monster);
		}
		// Deadly zones are kept out of always, light or not.
		Goal = PrologueMonsterRole::KeepOutOfHolyLightZones(Monster->GetWorld(), Goal, *Monster);

		// Onto the navmesh. A goal with no ground there (a ring place past the navmesh edge, backing off past the end
		// of the area) moves closer to the hero along the same line.
		FVector OnNav;
		bool bOnNav = PrologueMonsterRole::ProjectToNavigation(*Monster, Goal, OnNav);
		if (!bOnNav)
		{
			const FVector HeroLocation = Hero->GetActorLocation();
			for (const float Share : { 0.7f, 0.4f })
			{
				if (PrologueMonsterRole::ProjectToNavigation(*Monster, HeroLocation + (Goal - HeroLocation) * Share, OnNav))
				{
					bOnNav = true;
					break;
				}
			}
		}
		if (bOnNav)
		{
			Goal = OnNav;
		}

		const bool bAtGoal = FVector::Dist2D(Monster->GetActorLocation(), Goal) <= Acceptance;
		const bool bGoalShifted = !State.bHasGoal || FVector::Dist2D(Goal, State.LastGoal) > PrologueMonsterRole::RepathDistance;
		const bool bStopped = AI->GetMoveStatus() != EPathFollowingStatus::Moving;
		if (!bAtGoal && (bGoalShifted || bStopped) && Context.Time >= State.NextMoveTime)
		{
			const EPathFollowingRequestResult::Type Result = Monster->RequestMoveToLocation(AI, Goal, Acceptance);
			// Logged for a new goal only, not for every retry of the same one.
			if (Result == EPathFollowingRequestResult::Failed && bGoalShifted)
			{
				UE_LOG(LogTemp, Warning, TEXT("Prologue role: %s cannot walk to %s (%s, %s)"), *Monster->GetName(), *Goal.ToCompactString(),
					bRetreating ? TEXT("backing off") : TEXT("its place in the role"), bOnNav ? TEXT("no path") : TEXT("no navmesh there"));
			}
			State.LastGoal = Goal;
			State.bHasGoal = true;
			State.NextMoveTime = Context.Time + PrologueMonsterRole::MoveRequestInterval;
		}
	}

	AI->SetFocus(Hero);

	if (bFakeSwings && !bRetreating && Context.Time >= State.NextSwingTime)
	{
		// The very first pass only schedules, so a fresh group does not swing all at once.
		if (State.NextSwingTime > 0.0 && FVector::Dist2D(Monster->GetActorLocation(), Hero->GetActorLocation()) <= SwingDistance)
		{
			Monster->PlayAttackAnimation();
		}
		State.NextSwingTime = Context.Time + FMath::FRandRange(SwingIntervalMin, FMath::Max(SwingIntervalMin, SwingIntervalMax));
	}
}

// ---------------------------------------------------------------------------------------------------------------
// Run to hero

bool UPrologueMonsterRole_GoToHero::GetGoal(const FPrologueRoleContext& Context, const FPrologueRoleMemberState& State, FVector& OutGoal, float& OutAcceptance) const
{
	const FVector HeroLocation = Context.Hero->GetActorLocation();
	FVector Direction = Context.Monster->GetActorLocation() - HeroLocation;
	Direction.Z = 0.0f;
	if (!Direction.Normalize())
	{
		Direction = Context.Hero->GetActorForwardVector().GetSafeNormal2D();
	}
	OutGoal = HeroLocation + Direction * StopDistance;
	OutAcceptance = 60.0f;
	return true;
}

FString UPrologueMonsterRole_GoToHero::Describe() const
{
	return FString::Printf(TEXT("run to %s (stop at %.1f m)"), *HeroName(), StopDistance / 100.0f);
}

// ---------------------------------------------------------------------------------------------------------------
// Surround hero

bool UPrologueMonsterRole_Surround::GetGoal(const FPrologueRoleContext& Context, const FPrologueRoleMemberState& State, FVector& OutGoal, float& OutAcceptance) const
{
	float RingRadius = Radius;
	if (bRadiusFromHisLight)
	{
		const float Light = Context.Director->GetHeroLightRadius(Context.Hero);
		if (Light > 0.0f)
		{
			RingRadius = Light + Context.Monster->StalkSafetyBuffer + 30.0f;
		}
	}

	// In and out: 0 on the ring, PressDepth at the deepest point of the step.
	if (PressDepth > 0.0f)
	{
		const float Angle = 2.0f * PI * static_cast<float>(FMath::Fmod(Context.Time, static_cast<double>(PressPeriod))) / PressPeriod + State.Phase;
		const float Wave = 0.5f * (1.0f - FMath::Cos(Angle));
		RingRadius = FMath::Max(50.0f, RingRadius - PressDepth * Wave);
	}

	const float Yaw = State.SlotBaseYaw + 360.0f * State.SlotIndex / FMath::Max(1, State.SlotCount);
	OutGoal = Context.Hero->GetActorLocation() + FRotator(0.0f, Yaw, 0.0f).Vector() * RingRadius;
	OutAcceptance = 40.0f;
	return true;
}

// ---------------------------------------------------------------------------------------------------------------
// Sit on hero

UPrologueMonsterRole_SitOnHero::UPrologueMonsterRole_SitOnHero()
{
	// Pinning him down: swings at him now and then from up close.
	bFakeSwings = true;
	SwingDistance = 200.0f;
	SwingIntervalMin = 1.5f;
	SwingIntervalMax = 3.5f;
}

void UPrologueMonsterRole_SitOnHero::UpdateMember(const FPrologueRoleContext& Context, FPrologueRoleMemberState& State) const
{
	ABaseMonster* Monster = Context.Monster;
	ABaseCharacter* Hero = Context.Hero;
	ABaseCharacter* Previous = State.PassThroughHero.Get();
	if (Previous == Hero && (bPassThroughHero || !IsValid(Previous)))
	{
		return;
	}
	// The hero changed (an empty HeroId follows the player's hero) or the option is off: undo the old pair first.
	if (IsValid(Previous))
	{
		Monster->MoveIgnoreActorRemove(Previous);
		Previous->MoveIgnoreActorRemove(Monster);
		State.PassThroughHero.Reset();
	}
	if (bPassThroughHero)
	{
		Monster->MoveIgnoreActorAdd(Hero);
		Hero->MoveIgnoreActorAdd(Monster);
		State.PassThroughHero = Hero;
	}
}

bool UPrologueMonsterRole_SitOnHero::GetGoal(const FPrologueRoleContext& Context, const FPrologueRoleMemberState& State, FVector& OutGoal, float& OutAcceptance) const
{
	const FVector HeroLocation = Context.Hero->GetActorLocation();
	FVector Side = Context.Monster->GetActorLocation() - HeroLocation;
	Side.Z = 0.0f;
	if (!Side.Normalize())
	{
		Side = FVector::ForwardVector;
	}
	OutGoal = HeroLocation + Side * SitDistance;
	OutAcceptance = 30.0f;
	return true;
}

FString UPrologueMonsterRole_SitOnHero::Describe() const
{
	return FString::Printf(TEXT("sit on %s%s"), *HeroName(), bFakeSwings ? TEXT(", swings at him") : TEXT(""));
}

FString UPrologueMonsterRole_Surround::Describe() const
{
	return FString::Printf(TEXT("surround %s at %s%s%s"), *HeroName(),
		bRadiusFromHisLight ? TEXT("his light's edge") : *FString::Printf(TEXT("%.1f m"), Radius / 100.0f),
		bFakeSwings ? TEXT(", fake swings") : TEXT(""),
		bRetreatWhenHit ? *FString::Printf(TEXT(", driven off by light to %.0f m for %.0f s"), RetreatDistance / 100.0f, RetreatDuration) : TEXT(""));
}

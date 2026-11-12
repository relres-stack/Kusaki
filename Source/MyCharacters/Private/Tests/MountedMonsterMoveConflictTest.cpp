// Automated coverage for the mounted-monster AI-movement choke point — see ABaseMonster::
// RequestMoveToActor()/RequestMoveToLocation()'s own header comments for the full design. Before this
// fix, every call site that issued an AI nav-move request (CheckForPlayer(), StartStalking(),
// MoveToLightBorder(), MoveToNextPatrolPoint(), MoveToRandomWanderPoint(), the holy-light-flee reaction,
// TryTriggerEyePeekEncounter(), ReceiveAlert(), IssueShadowFleeMove()) called AICon->MoveToActor()/
// MoveToLocation() directly — while mounted, that fought the rider's own ABaseCharacter::
// AddMovementInput() passthrough and read as spinning/twitching. Tick()'s own bIsBeingRidden branch
// (StopMovement()/ClearFocus() every frame) only patched the SYMPTOM after a request had already been
// issued. The actual fix routes every one of those call sites through RequestMoveToActor()/
// RequestMoveToLocation() instead, which refuse outright (return Failed, never touch AICon at all)
// while bIsBeingRidden is true — structurally impossible to bypass from a future call site, since
// there's no separate bIsBeingRidden check left to forget.
//
// This test drives the choke point two ways:
//   (a) Directly, with bUsePathfinding=false so the result doesn't depend on this bare world having a
//       NavMesh (it doesn't) — the not-ridden control case must reach the real AIController and report
//       AlreadyAtGoal (Goal/Dest placed exactly at the monster's own location), while the ridden case
//       must report Failed without ever having reached it. This is the deterministic, NavMesh-
//       independent proof that the guard itself works.
//   (b) Through TryTriggerEyePeekEncounter() — a real call site that used to issue MoveToActor()
//       directly — force-called while ridden (bypassing the CheckForPlayer() gate that would normally
//       keep it from ever being reached in that state), confirming the function still runs (the choke
//       point lives inside the move-issuing helper, not as a blanket early-return on every function) but
//       leaves the AIController's own move status untouched. TryTriggerEyePeekEncounter()'s internal
//       call uses the default bUsePathfinding=true, so unlike (a) this can't distinguish "blocked by
//       riding" from "blocked by missing NavMesh" on its own — see EyePeekRiskTest.cpp's identical
//       BroadcastAlert()/NavMesh caveat — so it's kept as a smoke-level confirmation that nothing crashes
//       and the encounter still arms, on top of (a)'s already-conclusive proof.
//
// Same bare UWorld::CreateWorld() setup as EyePeekRiskTest.cpp/BullsChargeComboTest.cpp — component
// registration/collision happens during SpawnActor() itself, independent of BeginPlay, so this skips
// DispatchBeginPlay() same as those two files. Unlike those two, this test actually needs a real
// AAIController to observe: APawn::PostInitializeComponents()'s AutoPossessAI early-outs in this
// bare/non-startup world (its bPlacedInWorld/IsGameWorld() gating doesn't hold here the way it would for
// an actor placed in a real level), so each scenario below spawns and Possess()es its own AAIController
// explicitly rather than relying on auto-possession.
#include "Misc/AutomationTest.h"
#include "BaseCharacter.h"
#include "BaseMonster.h"
#include "AIController.h"
#include "Navigation/PathFollowingComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMountedMonsterMoveConflictTest, "MyCharacters.Mounting.MoveConflict", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FMountedMonsterMoveConflictTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("World"), World))
	{
		return false;
	}

	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);

	bool bPassed = true;

	// (a) Direct, deterministic proof: the choke point itself refuses while ridden, and only while ridden.
	{
		ABaseMonster* Monster = World->SpawnActor<ABaseMonster>();
		ABaseCharacter* GoalActor = World->SpawnActor<ABaseCharacter>();
		ABaseCharacter* Rider = World->SpawnActor<ABaseCharacter>();
		bPassed &= TestNotNull(TEXT("(a) Monster"), Monster) && TestNotNull(TEXT("(a) GoalActor"), GoalActor) && TestNotNull(TEXT("(a) Rider"), Rider);

		if (Monster && GoalActor && Rider)
		{
			AAIController* AICon = World->SpawnActor<AAIController>();
			bPassed &= TestNotNull(TEXT("(a) AAIController spawned"), AICon);
			if (AICon)
			{
				AICon->Possess(Monster);
			}

			if (AICon)
			{
				// Goal/Dest sit exactly on top of the monster — with bUsePathfinding=false this resolves
				// via AAIController::MoveTo()'s own HasReached() check, before any NavMesh would ever be
				// consulted, so AlreadyAtGoal here proves the request genuinely reached the AIController.
				GoalActor->SetActorLocation(Monster->GetActorLocation());
				const FVector Dest = Monster->GetActorLocation();

				// Not ridden yet — baseline: both requests must actually reach AICon.
				const EPathFollowingRequestResult::Type ActorResultBefore = Monster->RequestMoveToActor(
					AICon, GoalActor, 500.0f, true, /*bUsePathfinding=*/false);
				bPassed &= TestTrue(TEXT("(a) Not ridden — MoveToActor request reaches AICon (AlreadyAtGoal)"),
					ActorResultBefore == EPathFollowingRequestResult::AlreadyAtGoal);

				const EPathFollowingRequestResult::Type LocationResultBefore = Monster->RequestMoveToLocation(
					AICon, Dest, 500.0f, true, /*bUsePathfinding=*/false);
				bPassed &= TestTrue(TEXT("(a) Not ridden — MoveToLocation request reaches AICon (AlreadyAtGoal)"),
					LocationResultBefore == EPathFollowingRequestResult::AlreadyAtGoal);

				// Only sanctioned way to set bIsBeingRidden — the existing UMountRiderComponent-facing API,
				// not a raw field poke and not a new flag.
				Monster->OnMounted(Rider);
				bPassed &= TestTrue(TEXT("(a) bIsBeingRidden set by OnMounted()"), Monster->bIsBeingRidden);

				const EPathFollowingRequestResult::Type ActorResultRidden = Monster->RequestMoveToActor(
					AICon, GoalActor, 500.0f, true, /*bUsePathfinding=*/false);
				bPassed &= TestTrue(TEXT("(a) Ridden — MoveToActor request refused (Failed), never reaches AICon"),
					ActorResultRidden == EPathFollowingRequestResult::Failed);

				const EPathFollowingRequestResult::Type LocationResultRidden = Monster->RequestMoveToLocation(
					AICon, Dest, 500.0f, true, /*bUsePathfinding=*/false);
				bPassed &= TestTrue(TEXT("(a) Ridden — MoveToLocation request refused (Failed), never reaches AICon"),
					LocationResultRidden == EPathFollowingRequestResult::Failed);

				AICon->Destroy();
			}
		}

		if (Monster) { Monster->Destroy(); }
		if (GoalActor) { GoalActor->Destroy(); }
		if (Rider) { Rider->Destroy(); }
	}

	// (b) Realistic call site: force-calling TryTriggerEyePeekEncounter() (which used to issue
	// MoveToActor() directly) on a ridden monster must not crash, must still let the rest of the function
	// run (bHasActiveEyePeekEncounter arms), and must leave the AIController's own move status untouched.
	{
		ABaseMonster* Monster = World->SpawnActor<ABaseMonster>();
		ABaseCharacter* Player = World->SpawnActor<ABaseCharacter>();
		ABaseCharacter* Rider = World->SpawnActor<ABaseCharacter>();
		bPassed &= TestNotNull(TEXT("(b) Monster"), Monster) && TestNotNull(TEXT("(b) Player"), Player) && TestNotNull(TEXT("(b) Rider"), Rider);

		if (Monster && Player && Rider)
		{
			AAIController* AICon = World->SpawnActor<AAIController>();
			bPassed &= TestNotNull(TEXT("(b) AAIController spawned"), AICon);
			if (AICon)
			{
				AICon->Possess(Monster);
			}

			Monster->OnMounted(Rider);
			bPassed &= TestTrue(TEXT("(b) bIsBeingRidden set by OnMounted()"), Monster->bIsBeingRidden);

			if (AICon)
			{
				bPassed &= TestTrue(TEXT("(b) AICon idle before the forced call (OnMounted() already stopped it)"),
					AICon->GetMoveStatus() == EPathFollowingStatus::Idle);
			}

			// Forced call — CheckForPlayer() would normally never reach this while bIsBeingRidden, but the
			// task's whole point is that NO call site, existing or future, can slip an AI move request
			// through while mounted, so this simulates exactly that.
			Monster->TryTriggerEyePeekEncounter(Player);
			bPassed &= TestTrue(TEXT("(b) Encounter still arms — the choke point doesn't block the whole function"),
				Monster->bHasActiveEyePeekEncounter);

			if (AICon)
			{
				bPassed &= TestTrue(TEXT("(b) AICon still idle after — the MoveToActor() call never reached it"),
					AICon->GetMoveStatus() == EPathFollowingStatus::Idle);
				AICon->Destroy();
			}
		}

		if (Monster) { Monster->Destroy(); }
		if (Player) { Player->Destroy(); }
		if (Rider) { Rider->Destroy(); }
	}

	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);

	return bPassed;
}

#endif // WITH_DEV_AUTOMATION_TESTS

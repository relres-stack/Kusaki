// Automated coverage for the "Eye Peek Risk" mechanic — see ABaseMonster::TryTriggerEyePeekEncounter()/
// ResolveEyePeekReaction()/OnAttackHitNotify()/DoesEyePeekAttackConnect() header comments for the full
// design. A player who is nominally eyes-closed but currently peeking (bIsAiming, RMB held) can still be
// caught inside a monster's sight cone; once caught, the monster chases (MoveToActor()) and
// ResolveEyePeekReaction() POLLS every EyePeekReactionDelay seconds (a repeating timer, not a one-shot
// windup) until it's actually within EyePeekAttackRange — the swing never launches from farther away
// than that, and the whole encounter gives up after EyePeekMaxChaseDuration if it never gets close
// enough. Once in range, ResolveEyePeekReaction() decides ONE thing — Fatality (still peeking) vs.
// "swing blind" (eyes closed) — and NEVER applies the stun itself. The swing's connect/miss outcome is
// decided entirely later, at the swing's actual point of contact, by OnAttackHitNotify() (fired by the
// "Attack_Hit" AnimNotify on Anim_Whisper_Attack1):
//
//   (a) Still peeking (bIsAiming true) once in range                                -> Fatality
//   (b) Eyes closed, standing, still within EyePeekAttackRange at contact           -> Stun + Panic + Alert
//   (c) Eyes closed, standing, but OUT OF RANGE by the time contact happens         -> swing misses
//   (d) Eyes closed, CROUCHED at the moment of contact (even if otherwise in range) -> Dodge, unharmed
//   (e) Caught from outside EyePeekAttackRange                                      -> defers (no swing)
//       until a later poll finds the player actually in range
//
// This test drives TryTriggerEyePeekEncounter()/ResolveEyePeekReaction()/OnAttackHitNotify() directly
// rather than through CheckForPlayer()'s real ~0.1s sight-cone poll or an actual AnimNotify firing during
// real animation playback (neither of which this bare headless world can drive) — all three functions
// are public specifically so this can call them synchronously, same "promoted for a specific external
// caller" reasoning as BroadcastAlert(). Runs as a bare UWorld — HandleDeath()/EnterPanicState()/
// PlayAttackAnimation()/BroadcastAlert()/the overlap query DoesEyePeekAttackConnect() runs all work
// without BeginPlay or a GameMode (component registration/collision happens during SpawnActor() itself,
// independent of BeginPlay; none of these functions route through TakeDamage()/APawn::ShouldTakeDamage(),
// which is what needed the GameMode workaround in BullsChargeComboTest.cpp), so this test skips both —
// same minimal-setup precedent as that file's Monster/Obstacle spawns.
#include "Misc/AutomationTest.h"
#include "BaseCharacter.h"
#include "BaseMonster.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEyePeekRiskReactionTest, "MyCharacters.EyePeekRisk.ReactionCheck", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

namespace
{
	// Shared setup for each scenario below — a fresh Player/Monster pair so one scenario's outcome (e.g.
	// bIsDead) can never leak into the next. Catches the peek and resolves the reaction matrix up to (but
	// not including) OnAttackHitNotify() — callers handle that step themselves.
	bool ArmAndResolveEyePeek(FAutomationTestBase& Test, UWorld* World, bool bStillAimingAtContact, bool bCrouchedAtContact,
		ABaseCharacter*& OutPlayer, ABaseMonster*& OutMonster)
	{
		OutPlayer = World->SpawnActor<ABaseCharacter>();
		OutMonster = World->SpawnActor<ABaseMonster>();
		bool bPassed = Test.TestNotNull(TEXT("Player"), OutPlayer) && Test.TestNotNull(TEXT("Monster"), OutMonster);
		if (!bPassed)
		{
			return false;
		}

		// Catching the peek itself — TryTriggerEyePeekEncounter() only cares that Player is valid and no
		// encounter is already pending; it doesn't itself re-check aiming/eyes-closed (CheckForPlayer()'s
		// own call site does that gating before calling this).
		OutMonster->TryTriggerEyePeekEncounter(OutPlayer);
		bPassed &= Test.TestTrue(TEXT("Encounter armed after TryTriggerEyePeekEncounter"), OutMonster->bHasActiveEyePeekEncounter);

		// Set the player's state as of the moment the blind swing actually lands, then resolve directly
		// (bypassing EyePeekReactionTimerHandle's real EyePeekReactionDelay wait).
		OutPlayer->bIsAiming = bStillAimingAtContact;
		OutPlayer->SetIsCrouched(bCrouchedAtContact);
		OutMonster->ResolveEyePeekReaction(TWeakObjectPtr<ABaseCharacter>(OutPlayer));

		bPassed &= Test.TestFalse(TEXT("Encounter cleared after ResolveEyePeekReaction"), OutMonster->bHasActiveEyePeekEncounter);
		return bPassed;
	}
}

bool FEyePeekRiskReactionTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("World"), World))
	{
		return false;
	}

	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);

	bool bPassed = true;

	// (a) Eyes open at contact (still aiming) -> Fatality — resolves entirely inside
	// ResolveEyePeekReaction() itself, no AnimNotify/OnAttackHitNotify() involved.
	{
		ABaseCharacter* Player = nullptr;
		ABaseMonster* Monster = nullptr;
		bPassed &= ArmAndResolveEyePeek(*this, World, /*bStillAimingAtContact=*/true, /*bCrouchedAtContact=*/false, Player, Monster);
		if (Player)
		{
			bPassed &= TestTrue(TEXT("(a) Caught peeking -> Fatality"), Player->bIsDead);
			bPassed &= TestFalse(TEXT("(a) Not panicking — this was a kill, not a knockdown"), Player->bIsPanicking);
			Player->Destroy();
		}
		if (Monster)
		{
			Monster->Destroy();
		}
	}

	// (b) Eyes closed, standing, still in range at contact -> the swing connects: Stun + Panic (+ Alert;
	// BroadcastAlert()'s own pack-formation success depends on a real NavMesh this bare world doesn't
	// have, so only the directly observable Panic State activation is asserted here).
	{
		ABaseCharacter* Player = nullptr;
		ABaseMonster* Monster = nullptr;
		bPassed &= ArmAndResolveEyePeek(*this, World, /*bStillAimingAtContact=*/false, /*bCrouchedAtContact=*/false, Player, Monster);
		if (Player && Monster)
		{
			// This is the core of the bug fix: ResolveEyePeekReaction() itself must NOT have applied the
			// stun yet — it only starts the swing (PlayAttackAnimation()) and defers the ENTIRE
			// connect/miss decision (crouch AND range) to OnAttackHitNotify().
			bPassed &= TestFalse(TEXT("(b) Not panicking yet — ResolveEyePeekReaction() only starts the swing"), Player->bIsPanicking);

			// Player never moved — still at the monster's own spawn location, well within
			// EyePeekAttackRange (plus the forward offset) — so the "Attack_Hit" notify's contact check
			// should connect.
			Monster->OnAttackHitNotify();
			bPassed &= TestFalse(TEXT("(b) Not killed — the swing only knocked them down"), Player->bIsDead);
			bPassed &= TestTrue(TEXT("(b) Standing and in range at contact -> swing connects, Panic"), Player->bIsPanicking);
			Player->Destroy();
			Monster->Destroy();
		}
	}

	// (c) Eyes closed, standing, but OUT OF RANGE by the time the "Attack_Hit" notify fires -> the swing
	// connects on empty air: no Panic, no damage. This is the exact scenario the original bug got wrong:
	// the old code stunned the player regardless of this distance, "telepathically", the instant
	// ResolveEyePeekReaction() ran.
	{
		ABaseCharacter* Player = nullptr;
		ABaseMonster* Monster = nullptr;
		bPassed &= ArmAndResolveEyePeek(*this, World, /*bStillAimingAtContact=*/false, /*bCrouchedAtContact=*/false, Player, Monster);
		if (Player && Monster)
		{
			bPassed &= TestFalse(TEXT("(c) Not panicking yet — ResolveEyePeekReaction() only starts the swing"), Player->bIsPanicking);

			// Move well outside EyePeekAttackRange (200 units default) before the swing's contact point.
			Player->SetActorLocation(Monster->GetActorLocation() + FVector(2000.0f, 0.0f, 0.0f));
			Monster->OnAttackHitNotify();
			bPassed &= TestFalse(TEXT("(c) Not killed"), Player->bIsDead);
			bPassed &= TestFalse(TEXT("(c) Out of range at contact -> swing misses, no Panic"), Player->bIsPanicking);
			Player->Destroy();
			Monster->Destroy();
		}
	}

	// (d) Eyes closed, but CROUCHED at the moment of contact — even though otherwise well within
	// range — -> Dodge: unharmed, no panic. The crouch check now lives entirely inside
	// DoesEyePeekAttackConnect(), called from OnAttackHitNotify(), NOT as an early return inside
	// ResolveEyePeekReaction() — so the swing still plays (PendingEyePeekAttackTarget still gets set),
	// only the LATER contact check is what actually reads IsCrouched().
	{
		ABaseCharacter* Player = nullptr;
		ABaseMonster* Monster = nullptr;
		bPassed &= ArmAndResolveEyePeek(*this, World, /*bStillAimingAtContact=*/false, /*bCrouchedAtContact=*/true, Player, Monster);
		if (Player && Monster)
		{
			bPassed &= TestFalse(TEXT("(d) Not panicking yet — ResolveEyePeekReaction() only starts the swing"), Player->bIsPanicking);

			Monster->OnAttackHitNotify();
			bPassed &= TestFalse(TEXT("(d) Not killed"), Player->bIsDead);
			bPassed &= TestFalse(TEXT("(d) Crouched at contact -> Dodge, no panic even though in range"), Player->bIsPanicking);
			Player->Destroy();
			Monster->Destroy();
		}
	}

	// (e) Player caught peeking from OUTSIDE EyePeekAttackRange — ResolveEyePeekReaction() must defer
	// (NOT resolve, NOT play the swing) on the first poll, since the monster hasn't actually closed the
	// distance yet. This is the core of the "run up first" fix: the swing must never launch from
	// wherever the sight-cone check happened to trigger. Once Player is within range on a LATER poll, it
	// resolves normally.
	{
		ABaseCharacter* Player = World->SpawnActor<ABaseCharacter>();
		ABaseMonster* Monster = World->SpawnActor<ABaseMonster>();
		bPassed &= TestNotNull(TEXT("(e) Player"), Player) && TestNotNull(TEXT("(e) Monster"), Monster);
		if (Player && Monster)
		{
			Player->SetActorLocation(Monster->GetActorLocation() + FVector(2000.0f, 0.0f, 0.0f));
			Player->bIsAiming = false;
			Player->SetIsCrouched(false);

			Monster->TryTriggerEyePeekEncounter(Player);
			bPassed &= TestTrue(TEXT("(e) Encounter armed"), Monster->bHasActiveEyePeekEncounter);

			// Still 2000 units away — this poll must defer, not resolve.
			Monster->ResolveEyePeekReaction(TWeakObjectPtr<ABaseCharacter>(Player));
			bPassed &= TestTrue(TEXT("(e) Still approaching — encounter stays active, no swing yet"), Monster->bHasActiveEyePeekEncounter);
			bPassed &= TestFalse(TEXT("(e) No panic while still out of range"), Player->bIsPanicking);

			// Player is now within EyePeekAttackRange — the next poll should resolve (Fatality/swing
			// decision) rather than deferring again.
			Player->SetActorLocation(Monster->GetActorLocation());
			Monster->ResolveEyePeekReaction(TWeakObjectPtr<ABaseCharacter>(Player));
			bPassed &= TestFalse(TEXT("(e) Now in range — encounter resolves"), Monster->bHasActiveEyePeekEncounter);

			Player->Destroy();
			Monster->Destroy();
		}
	}

	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);

	return bPassed;
}

#endif // WITH_DEV_AUTOMATION_TESTS

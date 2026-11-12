// Automated coverage for the background "open eyes in sight = attack" rule — see
// ABaseMonster::TryStartChaseAttack()/PendingChaseAttackTarget's own header comments for the full design.
// Unlike Eye Peek Risk (nominally eyes-CLOSED player caught peeking) and Blind Encounter (eyes-closed
// physical touch), this is the background rule for the ordinary Patrol/Stalk/Chase state machine: a
// monster that catches the player with eyes genuinely OPEN within CatchDistance (CheckForPlayer(), gated
// by IsPlayerVisible()'s existing IsEyesClosed()==false check — untouched by this feature) no longer
// kills instantly. TryStartChaseAttack() starts the swing and defers the actual dodge-vs-hit decision to
// OnAttackHitNotify()/DoesEyePeekAttackConnect() — the SAME crouch+overlap check and the SAME "Attack_Hit"
// AnimNotify/fallback-timer infrastructure Eye Peek Risk already uses, just via a separate
// PendingChaseAttackTarget so the two attack sources never resolve each other's swing:
//
//   (a) Not crouched, still in range at the swing's real point of contact -> Stun: ABaseCharacter::
//       EnterPanicState() (same Nose-Breathing mini-game every other stun path already uses).
//   (b) Crouched BEFORE the swing starts and still crouched at contact (no stance change, eyes open)
//       -> HIT: eyes-open crouching is not a blind duck by itself (see DoesOpenEyesAttackConnect()'s own
//       comment) — Stun, same as (a).
//   (b2) Standing when the swing starts, crouches DURING the windup (stance changes before contact)
//       -> Dodge THIS swing only: routes into the existing Scanning state, same outcome as (c) below.
//   (b3) The very next swing after a (b2)-style dodge re-captures the player's now-crouched stance —
//       stays crouched, still eyes open -> connects normally (no lasting safety from one juke).
//   (c) Out of range at contact (a different kind of miss, same connect check) -> same Dodge/Scanning
//       outcome as (b2), since DoesOpenEyesAttackConnect() returns false for either reason.
//   (d) Mutual exclusion: force-calling TryStartChaseAttack() while an Eye Peek Risk swing is already
//       mid-flight (PendingEyePeekAttackTarget set) must not steal/overwrite that swing's resolution —
//       OnAttackHitNotify() must still resolve it as the Eye Peek outcome, not a chase outcome.
//   (e) A confirmed hit also costs ChaseAttackDamage Health via ABaseCharacter::TakeDamage() (through
//       UGameplayStatics::ApplyDamage()) — purely additive to (not a replacement for) EnterPanicState().
//   (f) A long enough series of survived hits (Panic reset between each, standing in for the Nose-
//       Breathing mini-game's own survival path — its own timings/odds are untouched, a separate balance
//       question) eventually kills via Health reaching 0, independent of the panic meter.
//   (g) Regression: a confirmed hit alone doesn't mark this monster "busy" the way a miss's
//       EnterScanningState() (bIsScanning) does — CheckForPlayer()'s own ~0.1s poll used to see the same
//       still-open-eyed, still-in-CatchDistance player again immediately after EnterPanicState() (which
//       knocks them down IN PLACE, not out of range) and call TryStartChaseAttack() again, restarting
//       PlayAttackAnimation() from frame 0 every poll — the swing visually never finished playing.
//       CheckForPlayer()'s top-level guard now also bails while Player->bIsPanicking, closing the gap.
//
// This test drives TryStartChaseAttack()/OnAttackHitNotify() (and, for scenario (d),
// TryTriggerEyePeekEncounter()/ResolveEyePeekReaction()) directly rather than through CheckForPlayer()'s
// real ~0.1s poll (which resolves Player via UGameplayStatics::GetPlayerCharacter() rather than an
// explicit parameter, needing a full PlayerController/possession setup this bare world doesn't have) —
// same "promoted for a specific external caller" reasoning, and same bare UWorld::CreateWorld() setup
// (no DispatchBeginPlay() — see EyePeekRiskTest.cpp's own comment for why it isn't needed for these
// particular functions) as every other automation test in this codebase. Scenarios (e)/(f) DO need one
// thing BullsChargeComboTest.cpp already established a workaround for: APawn::ShouldTakeDamage()
// unconditionally refuses damage without an authoritative GameMode (!GetWorld()->GetAuthGameMode()) —
// irrelevant in production (every real level has one) but this bare test world has none by default,
// which would otherwise make ApplyDamage() silently no-op regardless of whether the production damage
// code is correct — see that file's own comment on the CopyGameState() call below. Scenario (g) is the
// one exception to the "never through CheckForPlayer()" rule above — the bug it covers lives specifically
// in CheckForPlayer()'s own re-entry guard, so it needs a real APlayerController possessing Player for
// UGameplayStatics::GetPlayerCharacter() to resolve — see that scenario's own comment.
#include "Misc/AutomationTest.h"
#include "BaseCharacter.h"
#include "BaseMonster.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOpenEyesChaseAttackTest, "MyCharacters.OpenEyesChase.AttackResolution", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FOpenEyesChaseAttackTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("World"), World))
	{
		return false;
	}

	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);

	// See this file's own header comment — needed for scenarios (e)/(f)'s ApplyDamage() calls to actually
	// reach ABaseCharacter::TakeDamage() rather than being silently refused by APawn::ShouldTakeDamage().
	// CopyGameState() (not SetGameMode(), which derefs GetGameInstance() — null in this bare world context,
	// and would crash) is the public way to assign AuthorityGameMode directly, same as BullsChargeComboTest.cpp.
	World->CopyGameState(World->SpawnActor<AGameModeBase>(), nullptr);

	bool bPassed = true;

	// (a) Not crouched, in range (Player never moved from the monster's own spawn location) -> hit: Panic,
	// no Scanning.
	{
		ABaseCharacter* Player = World->SpawnActor<ABaseCharacter>();
		ABaseMonster* Monster = World->SpawnActor<ABaseMonster>();
		bPassed &= TestNotNull(TEXT("(a) Player"), Player) && TestNotNull(TEXT("(a) Monster"), Monster);
		if (Player && Monster)
		{
			Player->SetIsCrouched(false);
			Monster->TryStartChaseAttack(Player);
			bPassed &= TestFalse(TEXT("(a) Not panicking yet — TryStartChaseAttack() only starts the swing"), Player->bIsPanicking);

			Monster->OnAttackHitNotify();
			bPassed &= TestFalse(TEXT("(a) Not killed"), Player->bIsDead);
			bPassed &= TestTrue(TEXT("(a) Standing and in range at contact -> hit, Panic"), Player->bIsPanicking);
			bPassed &= TestFalse(TEXT("(a) A confirmed hit does not also enter Scanning"), Monster->bIsScanning);

			Player->Destroy();
			Monster->Destroy();
		}
	}

	// (b) Crouched BEFORE the swing starts and STILL crouched at contact (no stance change, eyes open)
	// -> HIT, not Dodge: eyes-open crouching is not a blind duck by itself — the fix for "monster swings
	// over your head while crouched with eyes open".
	{
		ABaseCharacter* Player = World->SpawnActor<ABaseCharacter>();
		ABaseMonster* Monster = World->SpawnActor<ABaseMonster>();
		bPassed &= TestNotNull(TEXT("(b) Player"), Player) && TestNotNull(TEXT("(b) Monster"), Monster);
		if (Player && Monster)
		{
			Player->SetIsCrouched(true);
			Monster->TryStartChaseAttack(Player);
			bPassed &= TestFalse(TEXT("(b) Not panicking yet — TryStartChaseAttack() only starts the swing"), Player->bIsPanicking);

			Monster->OnAttackHitNotify();
			bPassed &= TestFalse(TEXT("(b) Not killed"), Player->bIsDead);
			bPassed &= TestTrue(TEXT("(b) Crouched the whole time, eyes open, in range -> hit low, Panic"), Player->bIsPanicking);
			bPassed &= TestFalse(TEXT("(b) A confirmed hit does not also enter Scanning"), Monster->bIsScanning);

			Player->Destroy();
			Monster->Destroy();
		}
	}

	// (b2) Standing when the swing starts, crouches DURING the windup (before contact) -> Dodge THIS
	// swing (stance no longer matches what the swing committed to) — routes into Scanning.
	// (b3) The VERY NEXT swing re-captures the now-crouched stance and connects normally if the player
	// stays crouched — one juke buys one miss, not lasting safety.
	{
		ABaseCharacter* Player = World->SpawnActor<ABaseCharacter>();
		ABaseMonster* Monster = World->SpawnActor<ABaseMonster>();
		bPassed &= TestNotNull(TEXT("(b2) Player"), Player) && TestNotNull(TEXT("(b2) Monster"), Monster);
		if (Player && Monster)
		{
			Player->SetIsCrouched(false);
			Monster->TryStartChaseAttack(Player);

			Player->SetIsCrouched(true);
			Monster->OnAttackHitNotify();
			bPassed &= TestFalse(TEXT("(b2) Not killed"), Player->bIsDead);
			bPassed &= TestFalse(TEXT("(b2) Crouched mid-swing -> Dodge THIS swing despite eyes open"), Player->bIsPanicking);
			bPassed &= TestTrue(TEXT("(b2) Dodge routes into Scanning"), Monster->bIsScanning);

			Monster->TryStartChaseAttack(Player);
			Monster->OnAttackHitNotify();
			bPassed &= TestTrue(TEXT("(b3) Next swing re-targets the now-crouched stance -> hits (stayed crouched)"), Player->bIsPanicking);

			Player->Destroy();
			Monster->Destroy();
		}
	}

	// (c) Standing, but moved well OUT of range before the swing's contact point -> same Dodge/Scanning
	// outcome as (b), since DoesEyePeekAttackConnect() returns false for either reason.
	{
		ABaseCharacter* Player = World->SpawnActor<ABaseCharacter>();
		ABaseMonster* Monster = World->SpawnActor<ABaseMonster>();
		bPassed &= TestNotNull(TEXT("(c) Player"), Player) && TestNotNull(TEXT("(c) Monster"), Monster);
		if (Player && Monster)
		{
			Player->SetIsCrouched(false);
			Monster->TryStartChaseAttack(Player);

			Player->SetActorLocation(Monster->GetActorLocation() + FVector(2000.0f, 0.0f, 0.0f));
			Monster->OnAttackHitNotify();
			bPassed &= TestFalse(TEXT("(c) Not killed"), Player->bIsDead);
			bPassed &= TestFalse(TEXT("(c) Out of range at contact -> Dodge, no panic"), Player->bIsPanicking);
			bPassed &= TestTrue(TEXT("(c) Miss (out of range) also routes into Scanning"), Monster->bIsScanning);

			Player->Destroy();
			Monster->Destroy();
		}
	}

	// (d) Mutual exclusion: an Eye Peek Risk swing already in flight (PendingEyePeekAttackTarget set via
	// its own real entry path) must not be stolen/overwritten by a forced TryStartChaseAttack() call —
	// OnAttackHitNotify() must still resolve it as the Eye Peek outcome.
	{
		ABaseCharacter* Player = World->SpawnActor<ABaseCharacter>();
		ABaseMonster* Monster = World->SpawnActor<ABaseMonster>();
		bPassed &= TestNotNull(TEXT("(d) Player"), Player) && TestNotNull(TEXT("(d) Monster"), Monster);
		if (Player && Monster)
		{
			// Arms Eye Peek Risk's own PendingEyePeekAttackTarget via its real entry path: caught peeking,
			// then released RMB (eyes closed, not aiming) once "in range" -> ResolveEyePeekReaction()'s
			// "swing blind" branch sets PendingEyePeekAttackTarget and starts the swing.
			Monster->TryTriggerEyePeekEncounter(Player);
			Player->bIsAiming = false;
			Player->SetIsCrouched(false);
			Monster->ResolveEyePeekReaction(TWeakObjectPtr<ABaseCharacter>(Player));

			// Forced call — simulates some OTHER code path also trying to start a background-chase swing on
			// the same monster while its Eye Peek swing is already mid-flight; must no-op rather than
			// stomping PendingEyePeekAttackTarget.
			Monster->TryStartChaseAttack(Player);

			Monster->OnAttackHitNotify();
			bPassed &= TestFalse(TEXT("(d) Not killed"), Player->bIsDead);
			bPassed &= TestTrue(TEXT("(d) Eye Peek swing still resolves as a hit (Panic) despite the forced TryStartChaseAttack() call"), Player->bIsPanicking);

			Player->Destroy();
			Monster->Destroy();
		}
	}

	// (e) A single confirmed hit costs exactly ChaseAttackDamage worth of Health, on top of (not instead
	// of) the existing Panic State — Health/Panic are two independent stakes now, not one.
	{
		ABaseCharacter* Player = World->SpawnActor<ABaseCharacter>();
		ABaseMonster* Monster = World->SpawnActor<ABaseMonster>();
		bPassed &= TestNotNull(TEXT("(e) Player"), Player) && TestNotNull(TEXT("(e) Monster"), Monster);
		if (Player && Monster)
		{
			const float HealthBefore = Player->Health;
			Player->SetIsCrouched(false);
			Monster->TryStartChaseAttack(Player);
			Monster->OnAttackHitNotify();

			bPassed &= TestTrue(TEXT("(e) Hit still enters Panic (damage is additive, not a replacement)"), Player->bIsPanicking);
			bPassed &= TestEqual(TEXT("(e) Hit costs exactly ChaseAttackDamage Health"), Player->Health, HealthBefore - Monster->ChaseAttackDamage, 0.01f);

			Player->Destroy();
			Monster->Destroy();
		}
	}

	// (f) A long enough series of hits kills via Health reaching 0 even if Panic is "survived" every time
	// (bIsPanicking reset between hits, standing in for the Nose-Breathing mini-game's own survival path —
	// that mini-game's own timings/odds are a separate balance question this doesn't touch) — Health is a
	// real, independent stake now, not just the panic meter.
	{
		ABaseCharacter* Player = World->SpawnActor<ABaseCharacter>();
		ABaseMonster* Monster = World->SpawnActor<ABaseMonster>();
		bPassed &= TestNotNull(TEXT("(f) Player"), Player) && TestNotNull(TEXT("(f) Monster"), Monster);
		if (Player && Monster)
		{
			Player->SetIsCrouched(false);
			const int32 MaxHitsToAttempt = FMath::CeilToInt32(Player->MaxHealth / Monster->ChaseAttackDamage) + 1;
			for (int32 HitIndex = 0; HitIndex < MaxHitsToAttempt && !Player->bIsDead; ++HitIndex)
			{
				// Stands in for the panic mini-game resolving safely each time, so this hit's own
				// EnterPanicState() call doesn't just no-op against an already-panicking player.
				Player->bIsPanicking = false;
				Monster->TryStartChaseAttack(Player);
				Monster->OnAttackHitNotify();
			}

			bPassed &= TestTrue(TEXT("(f) Enough survived hits eventually kill via Health, independent of the panic meter"), Player->bIsDead);

			Player->Destroy();
			Monster->Destroy();
		}
	}

	// (g) Regression: while the just-hit player is still panicking, CheckForPlayer()'s own ~0.1s poll must
	// NOT start a second swing — see this file's own header comment for the exact bug this closes. Needs
	// a real APlayerController possessing Player so UGameplayStatics::GetPlayerCharacter() (what
	// CheckForPlayer() itself resolves Player through, unlike every other scenario above) finds it.
	{
		ABaseCharacter* Player = World->SpawnActor<ABaseCharacter>();
		ABaseMonster* Monster = World->SpawnActor<ABaseMonster>();
		APlayerController* PC = World->SpawnActor<APlayerController>();
		bPassed &= TestNotNull(TEXT("(g) Player"), Player) && TestNotNull(TEXT("(g) Monster"), Monster) && TestNotNull(TEXT("(g) PlayerController"), PC);
		if (Player && Monster && PC)
		{
			// AController::PostInitializeComponents() (which is what normally registers a spawned
			// APlayerController into World->PlayerControllerList, what UGameplayStatics::
			// GetPlayerController()/GetPlayerCharacter() actually search) doesn't reliably run for an actor
			// spawned into this bare world outside a real level-start flow — same underlying gap
			// MountedMonsterMoveConflictTest.cpp hit with ABaseMonster's AutoPossessAI, worked around there
			// by possessing explicitly instead of relying on it. AddController() is the direct, public
			// equivalent for a PlayerController specifically.
			World->AddController(PC);
			PC->Possess(Player);
			bPassed &= TestTrue(TEXT("(g) GetPlayerCharacter() resolves to our spawned Player"),
				UGameplayStatics::GetPlayerCharacter(Monster, 0) == Player);

			// Directly in front, well within both FieldOfViewAngle and CatchDistance — CheckForPlayer()'s
			// own IsPlayerVisible()/sight-cone geometry check (untouched by this fix) needs a real angle/
			// distance to pass, unlike every scenario above which calls TryStartChaseAttack() directly and
			// skips that geometry entirely.
			Player->SetActorLocation(Monster->GetActorLocation() + Monster->GetActorForwardVector() * 50.0f);
			Player->SetIsCrouched(false);

			// First hit, driven through the real CheckForPlayer() entry point this time (not
			// TryStartChaseAttack() directly) — this IS the exact path the bug lived in.
			Monster->CheckForPlayer();
			Monster->OnAttackHitNotify();
			bPassed &= TestTrue(TEXT("(g) First hit lands (Panic)"), Player->bIsPanicking);
			const float HealthAfterFirstHit = Player->Health;
			bPassed &= TestTrue(TEXT("(g) First hit actually cost Health"), HealthAfterFirstHit < Player->MaxHealth);

			// Bug reproduction: poll again immediately, exactly like the real ~0.1s loop would, while
			// Player is STILL panicking and still sitting in place well within CatchDistance. Before the
			// fix this called TryStartChaseAttack() again, re-arming PendingChaseAttackTarget; after the
			// fix, CheckForPlayer()'s guard bails before ever reaching that branch. OnAttackHitNotify()
			// right after is the observable proof either way: if a second swing HAD been armed, this
			// resolves it (a second hit's worth of Health lost); if not, it's a no-op against nothing
			// pending.
			Monster->CheckForPlayer();
			Monster->OnAttackHitNotify();
			bPassed &= TestEqual(TEXT("(g) No second swing starts (no second hit's Health lost) while still panicking"),
				Player->Health, HealthAfterFirstHit, 0.01f);

			// Panic ends (stands in for the mini-game resolving, same convention as scenario (f)) — normal
			// detection/attack must resume rather than staying permanently suppressed.
			Player->bIsPanicking = false;
			Monster->CheckForPlayer();
			Monster->OnAttackHitNotify();
			bPassed &= TestTrue(TEXT("(g) Once panic ends, detection/attack resumes — a real second hit lands"),
				Player->Health < HealthAfterFirstHit);

			Player->Destroy();
			Monster->Destroy();
			PC->Destroy();
		}
	}

	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);

	return bPassed;
}

#endif // WITH_DEV_AUTOMATION_TESTS

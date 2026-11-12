// Automated coverage for Redneck's 2-tier "Moonshine & Rage Dash"/"Bulls Charge" system (Таран) — see
// UMoonshineComponent's own header comment for the full Tier 1 (buff)/Tier 2 (aimed dash) design:
//
//   Tier 1 — UMoonshineComponent::UseMoonshine(), called from the inventory consumable slot, activates
//   ONLY the buff (bIsMoonshineActive + a SprintSpeed boost + the passive OnOwnerCapsuleHit() ram-on-
//   touch). It is NEVER called from aiming (RMB) or attacking (LMB) — those only ever READ
//   bIsMoonshineActive.
//
//   Tier 2 — ARedneckCharacter::TryHijackAttackPress() (RMB held + LMB press) fires
//   UMoonshineComponent::PerformRageDash() — a deliberate directional lunge — ONLY if the Tier 1 buff is
//   already active; with the buff inactive, RMB+LMB falls through to ThrowItem()'s ordinary aimed
//   throw/attack instead (no dash).
//
// Also covers UMoonshineComponent::OnOwnerCapsuleHit()'s ram interaction (shared by both the passive
// Tier-1 bump and a Tier-2 dash collision) with both an ABaseMonster (knockback/stagger) and a
// non-monster actor (generic TakeDamage(), standing in for a future destructible/obstacle class like a
// door).
//
// Also covers Roadmap 1.3's three side effects UseMoonshine() applies to the owning ABaseCharacter: eyes
// forced open AND locked open (ABaseCharacter::ForceEyesOpen()/SetEyesLockedOpen()), full immunity to
// monster damage/panic (SetImmuneToMonsterAttacks(), checked at the top of TakeDamage()/
// EnterPanicState()), and an auto-started sprint. Only the ON state — the OFF transition happens in
// EndMoonshine(), which only fires off MoonshineTimerHandle expiring, and this bare test world never
// ticks (see below), so there's no clock to drive that here.
//
// Runs as a bare UWorld, with DispatchBeginPlay() called explicitly on each spawned actor — a bare
// UWorld::CreateWorld() world never runs the full InitializeActorsForPlay()/GameMode-driven startup a
// real level load does, so SpawnActor() alone does not reliably run BeginPlay() the way it would in a
// real PIE/game session; calling DispatchBeginPlay() directly is what actually runs it here. That's
// required for this test specifically (unlike SprintSystemTest.cpp, which never needs it): only
// UMoonshineComponent::BeginPlay() wires its OnOwnerCapsuleHit() to the owning capsule's OnComponentHit
// delegate, and the hit itself is simulated below by broadcasting that same delegate directly rather
// than needing a real physics-simulated collision sweep.
#include "Misc/AutomationTest.h"
#include "RedneckCharacter.h"
#include "MoonshineComponent.h"
#include "BaseMonster.h"
#include "BaseCharacter.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/GameModeBase.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBullsChargeComboTest, "MyCharacters.Redneck.BullsChargeCombo", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBullsChargeComboTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("World"), World))
	{
		return false;
	}

	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);

	// APawn::ShouldTakeDamage() unconditionally refuses damage without an authoritative GameMode
	// (!GetWorld()->GetAuthGameMode()) — irrelevant to production (every real level has one), but this
	// bare test world has none by default, which would otherwise make the obstacle-ram step below
	// (damaging a Pawn-derived "obstacle" stand-in) silently no-op regardless of whether the production
	// ram code is correct. CopyGameState() is used (not SetGameMode(), which derefs GetGameInstance() —
	// null in this bare world context, and would crash) purely as a public way to assign
	// AuthorityGameMode directly.
	World->CopyGameState(World->SpawnActor<AGameModeBase>(), nullptr);

	ARedneckCharacter* Redneck = World->SpawnActor<ARedneckCharacter>();
	bool bPassed = TestNotNull(TEXT("Redneck"), Redneck);
	if (IsValid(Redneck))
	{
		// See this file's own header comment on why this explicit call (not just SpawnActor() alone) is
		// needed for BeginPlay() to actually run in a bare test world.
		Redneck->DispatchBeginPlay();
		bPassed &= TestTrue(TEXT("Redneck has begun play"), Redneck->HasActorBegunPlay());
	}

	if (bPassed)
	{
		UMoonshineComponent* Moonshine = Redneck->MoonshineComponent;
		bPassed &= TestNotNull(TEXT("MoonshineComponent"), Moonshine);

		if (bPassed)
		{
			const float DefaultSprintSpeed = Redneck->SprintSpeed;

			// 1. RMB+LMB with the buff NOT active: TryHijackAttackPress() must return false (no dash) and
			// must NOT activate/spend the buff itself — aiming/attacking only ever READ bIsMoonshineActive,
			// they never call UseMoonshine(). ThrowItem() (protected, not callable directly from this
			// test) would fall through to its own normal aimed throw/attack.
			Redneck->bIsAiming = true;
			const bool bHijackedWithoutBuff = Redneck->TryHijackAttackPress();
			bPassed &= TestFalse(TEXT("No dash on RMB+LMB while the buff is inactive"), bHijackedWithoutBuff);
			bPassed &= TestFalse(TEXT("Attacking does not activate the buff itself"), Moonshine->bIsMoonshineActive);
			bPassed &= TestTrue(TEXT("Still ready to drink — attacking never spends it"), Moonshine->bCanDrinkMoonshine);
			Redneck->bIsAiming = false;

			// 2. Tier 1 — activate the buff directly (simulating "used the item from the inventory
			// slot", independent of aiming/attacking entirely): boosts SprintSpeed by MoonshineSpeedBoost,
			// does NOT move the character by itself.
			const bool bDrankSuccessfully = Moonshine->UseMoonshine();
			bPassed &= TestTrue(TEXT("UseMoonshine succeeds when ready"), bDrankSuccessfully);
			bPassed &= TestTrue(TEXT("Buff active after UseMoonshine"), Moonshine->bIsMoonshineActive);
			bPassed &= TestFalse(TEXT("On cooldown gate immediately after drinking"), Moonshine->bCanDrinkMoonshine);
			bPassed &= TestEqual(TEXT("SprintSpeed boosted by MoonshineSpeedBoost"),
				Redneck->SprintSpeed, DefaultSprintSpeed * (1.0f + Moonshine->MoonshineSpeedBoost));

			// 2b. The buff's three side effects on ABaseCharacter (added alongside 1.3): eyes forced open
			// and locked (can't CloseEyes() again until the buff ends), full immunity to monster damage/
			// panic (TakeDamage()/EnterPanicState() both no-op), and an auto-started sprint. Only the ON
			// state is checked here — verifying the OFF transition would need EndMoonshine() to actually
			// fire, which only happens via MoonshineTimerHandle expiring, and this bare test world never
			// ticks (see this file's own header comment), so there's no clock driving it forward.
			bPassed &= TestFalse(TEXT("Eyes forced open by the buff"), Redneck->IsEyesClosed());
			bPassed &= TestTrue(TEXT("Eyes locked open for the buff's duration"), Redneck->AreEyesLockedOpen());
			Redneck->CloseEyes();
			bPassed &= TestFalse(TEXT("CloseEyes() refused while eyes are locked open"), Redneck->IsEyesClosed());
			bPassed &= TestTrue(TEXT("Immune to monster attacks while the buff is active"), Redneck->IsImmuneToMonsterAttacks());
			bPassed &= TestTrue(TEXT("Auto-started sprinting when the buff landed"), Redneck->bIsSprinting);

			// 3. Tier 2 — RMB (Aim) held + LMB (Attack) press, buff NOW active: TryHijackAttackPress()
			// must return true (consuming the press, no fall-through throw) and fire PerformRageDash(),
			// observable as a pending launch velocity on the movement component (ACharacter::
			// LaunchCharacter() -> UCharacterMovementComponent::Launch() stores it in
			// PendingLaunchVelocity, consumed on the next movement tick — this bare test world never
			// ticks, so this is the earliest observable proof the dash was actually issued).
			Redneck->bIsAiming = true;
			UCharacterMovementComponent* MoveComp = Redneck->GetCharacterMovement();
			bPassed &= TestNotNull(TEXT("MovementComponent"), MoveComp);
			if (IsValid(MoveComp))
			{
				// UCharacterMovementComponent::Launch() (called by ACharacter::LaunchCharacter(), which
				// PerformRageDash() uses) silently no-ops while MovementMode == MOVE_None — normally
				// already MOVE_Walking on any possessed character, but this bare test world never ticks,
				// so nothing has driven that assignment yet.
				MoveComp->SetMovementMode(MOVE_Walking);
				// UCharacterMovementComponent::Launch() also requires IsActive() — false here because
				// this bare test world never ticks/possesses this pawn through the normal flow that
				// would activate it.
				MoveComp->Activate(true);
			}
			const bool bHijackedWithBuff = Redneck->TryHijackAttackPress();
			bPassed &= TestTrue(TEXT("Dash fires on RMB+LMB while the buff is active"), bHijackedWithBuff);
			if (IsValid(MoveComp))
			{
				bPassed &= TestFalse(TEXT("Rage Dash sets a pending launch velocity"),
					MoveComp->PendingLaunchVelocity.IsNearlyZero());
			}
			// The buff itself is untouched by the dash — still active, same cooldown gate as after step 2.
			bPassed &= TestTrue(TEXT("Buff still active immediately after dashing"), Moonshine->bIsMoonshineActive);

			// 4. Ram vs. a monster (either the Tier-1 passive bump or a Tier-2 dash collision — both go
			// through this same handler): ApplyKnockback() staggers it (bIsStaggered) instead of just
			// blocking movement — simulated by broadcasting the exact OnComponentHit delegate
			// UMoonshineComponent::BeginPlay() bound to Redneck's capsule, standing in for a real physics
			// collision sweep.
			ABaseMonster* Monster = World->SpawnActor<ABaseMonster>();
			bPassed &= TestNotNull(TEXT("Monster"), Monster);
			if (IsValid(Monster))
			{
				Monster->SetActorLocation(Redneck->GetActorLocation() + FVector(150.0f, 0.0f, 0.0f));
				FHitResult DummyHit;
				Redneck->GetCapsuleComponent()->OnComponentHit.Broadcast(
					Redneck->GetCapsuleComponent(), Monster, Monster->GetCapsuleComponent(), FVector::ZeroVector, DummyHit);
				bPassed &= TestTrue(TEXT("Ram staggers a hit monster"), Monster->bIsStaggered);
				Monster->Destroy();
			}

			// 5. Ram vs. a non-monster actor: takes RamDamage via generic TakeDamage() — the "destructible/
			// obstacle" branch, standing in for a future Door/obstacle class (none exists in this project
			// yet). Reuses a plain ABaseCharacter (not a monster) purely as a TakeDamage()-capable target.
			ABaseCharacter* Obstacle = World->SpawnActor<ABaseCharacter>();
			bPassed &= TestNotNull(TEXT("Obstacle"), Obstacle);
			if (IsValid(Obstacle))
			{
				const float HealthBefore = Obstacle->Health;
				Obstacle->SetActorLocation(Redneck->GetActorLocation() + FVector(-150.0f, 0.0f, 0.0f));
				FHitResult DummyHit;
				Redneck->GetCapsuleComponent()->OnComponentHit.Broadcast(
					Redneck->GetCapsuleComponent(), Obstacle, Obstacle->GetCapsuleComponent(), FVector::ZeroVector, DummyHit);
				bPassed &= TestTrue(TEXT("Ram damages a non-monster obstacle actor"), Obstacle->Health < HealthBefore);
				Obstacle->Destroy();
			}
		}

		Redneck->Destroy();
	}

	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);

	return bPassed;
}

#endif // WITH_DEV_AUTOMATION_TESTS

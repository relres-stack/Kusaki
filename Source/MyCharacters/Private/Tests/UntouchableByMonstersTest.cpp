// Automated coverage for ABaseCharacter::SetUntouchableByMonsters() (task 04, step 2.7) — the prologue's "monsters
// cannot harm this hero at all" flag, set by APrologueDirector on heroes the player is not controlling, on a hero
// lying after a scripted fall and on whoever a scenario step protects. Separate from Moonshine's
// SetImmuneToMonsterAttacks(), which only blocks damage and panic; this one also blocks every instant kill (all of
// them end in ABaseMonster::OnKillPlayer()), the touch and the catch-range swing itself.
//
//   (a) A chase swing that connects: no Health lost, no panic, alive.
//   (b) Eye Peek Fatality (still peeking when the blind swing lands): alive.
//   (c) Blind Encounter kill (ResolveBlindEncounterKill()): alive while protected; once the flag is off the same
//       monster kills normally — the guard sits before bHasCaughtPlayer, so the monster is not left "has caught".
//   (d) CheckForPlayer() at catch range: no swing starts at a protected hero; once the flag is off it does.
//   (e) Independence from Moonshine: SetImmuneToMonsterAttacks(false) (what EndMoonshine() calls) leaves the flag on.
//
// Same bare UWorld setup as OpenEyesChaseAttackTest.cpp (see its header comment for CopyGameState() and the
// APlayerController workaround used by (d)).
#include "Misc/AutomationTest.h"
#include "BaseCharacter.h"
#include "BaseMonster.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUntouchableByMonstersTest, "MyCharacters.Prologue.UntouchableByMonsters", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FUntouchableByMonstersTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("World"), World))
	{
		return false;
	}

	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);
	// ApplyDamage() needs an authoritative GameMode to reach TakeDamage() at all (see OpenEyesChaseAttackTest.cpp).
	World->CopyGameState(World->SpawnActor<AGameModeBase>(), nullptr);

	bool bPassed = true;

	// (a) Chase swing connects on a protected hero.
	{
		ABaseCharacter* Player = World->SpawnActor<ABaseCharacter>();
		ABaseMonster* Monster = World->SpawnActor<ABaseMonster>();
		bPassed &= TestNotNull(TEXT("(a) Player"), Player) && TestNotNull(TEXT("(a) Monster"), Monster);
		if (Player && Monster)
		{
			Player->SetUntouchableByMonsters(true);
			Player->SetIsCrouched(false);
			const float HealthBefore = Player->Health;
			Monster->TryStartChaseAttack(Player);
			Monster->OnAttackHitNotify();

			bPassed &= TestEqual(TEXT("(a) No Health lost"), Player->Health, HealthBefore, 0.01f);
			bPassed &= TestFalse(TEXT("(a) No panic"), Player->bIsPanicking);
			bPassed &= TestFalse(TEXT("(a) Alive"), Player->bIsDead);

			Player->Destroy();
			Monster->Destroy();
		}
	}

	// (b) Eye Peek Fatality: still aiming when the reaction resolves.
	{
		ABaseCharacter* Player = World->SpawnActor<ABaseCharacter>();
		ABaseMonster* Monster = World->SpawnActor<ABaseMonster>();
		bPassed &= TestNotNull(TEXT("(b) Player"), Player) && TestNotNull(TEXT("(b) Monster"), Monster);
		if (Player && Monster)
		{
			Player->SetUntouchableByMonsters(true);
			Monster->TryTriggerEyePeekEncounter(Player);
			Player->bIsAiming = true;
			Monster->ResolveEyePeekReaction(TWeakObjectPtr<ABaseCharacter>(Player));

			bPassed &= TestFalse(TEXT("(b) Eye Peek Fatality does not kill a protected hero"), Player->bIsDead);

			Player->Destroy();
			Monster->Destroy();
		}
	}

	// (c) Blind Encounter kill, then the same monster once the protection is off.
	{
		ABaseCharacter* Player = World->SpawnActor<ABaseCharacter>();
		ABaseMonster* Monster = World->SpawnActor<ABaseMonster>();
		bPassed &= TestNotNull(TEXT("(c) Player"), Player) && TestNotNull(TEXT("(c) Monster"), Monster);
		if (Player && Monster)
		{
			Player->SetUntouchableByMonsters(true);
			Monster->ResolveBlindEncounterKill(Player);
			bPassed &= TestFalse(TEXT("(c) Blind Encounter kill does not kill a protected hero"), Player->bIsDead);

			Player->SetUntouchableByMonsters(false);
			Monster->ResolveBlindEncounterKill(Player);
			bPassed &= TestTrue(TEXT("(c) Protection off - the same monster kills normally"), Player->bIsDead);

			Player->Destroy();
			Monster->Destroy();
		}
	}

	// (d) CheckForPlayer() at catch range: no swing at a protected hero.
	{
		ABaseCharacter* Player = World->SpawnActor<ABaseCharacter>();
		ABaseMonster* Monster = World->SpawnActor<ABaseMonster>();
		APlayerController* PC = World->SpawnActor<APlayerController>();
		bPassed &= TestNotNull(TEXT("(d) Player"), Player) && TestNotNull(TEXT("(d) Monster"), Monster) && TestNotNull(TEXT("(d) PlayerController"), PC);
		if (Player && Monster && PC)
		{
			World->AddController(PC);
			PC->Possess(Player);
			bPassed &= TestTrue(TEXT("(d) GetPlayerCharacter() resolves to our spawned Player"),
				UGameplayStatics::GetPlayerCharacter(Monster, 0) == Player);

			Player->SetActorLocation(Monster->GetActorLocation() + Monster->GetActorForwardVector() * 50.0f);
			Player->SetIsCrouched(false);
			Player->SetUntouchableByMonsters(true);
			const float HealthBefore = Player->Health;

			Monster->CheckForPlayer();
			Monster->OnAttackHitNotify();
			bPassed &= TestEqual(TEXT("(d) Protected: no swing, no Health lost"), Player->Health, HealthBefore, 0.01f);
			bPassed &= TestFalse(TEXT("(d) Protected: no panic"), Player->bIsPanicking);

			Player->SetUntouchableByMonsters(false);
			Monster->CheckForPlayer();
			Monster->OnAttackHitNotify();
			bPassed &= TestTrue(TEXT("(d) Protection off - the swing lands"), Player->Health < HealthBefore);
			bPassed &= TestTrue(TEXT("(d) Protection off - panic"), Player->bIsPanicking);

			Player->Destroy();
			Monster->Destroy();
			PC->Destroy();
		}
	}

	// (e) Moonshine turning its own flag off does not take this protection away.
	{
		ABaseCharacter* Player = World->SpawnActor<ABaseCharacter>();
		ABaseMonster* Monster = World->SpawnActor<ABaseMonster>();
		bPassed &= TestNotNull(TEXT("(e) Player"), Player) && TestNotNull(TEXT("(e) Monster"), Monster);
		if (Player && Monster)
		{
			Player->SetUntouchableByMonsters(true);
			Player->SetImmuneToMonsterAttacks(true);
			Player->SetImmuneToMonsterAttacks(false);
			bPassed &= TestTrue(TEXT("(e) Still untouchable after the Moonshine flag goes off"), Player->IsUntouchableByMonsters());

			const float HealthBefore = Player->Health;
			Player->SetIsCrouched(false);
			Monster->TryStartChaseAttack(Player);
			Monster->OnAttackHitNotify();
			bPassed &= TestEqual(TEXT("(e) Still no Health lost"), Player->Health, HealthBefore, 0.01f);

			Player->Destroy();
			Monster->Destroy();
		}
	}

	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);

	return bPassed;
}

#endif // WITH_DEV_AUTOMATION_TESTS

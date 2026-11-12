// Automated coverage for the Sprint/Walk/Crouch speed system (see BaseCharacter.h's
// bIsSprinting/DefaultWalkSpeed/SprintSpeed and ToggleSprint()/StartSprinting()/StopSprinting()):
// default movement speeds, Left Alt's toggle behavior, and Sprint being blocked/auto-cancelled by
// crouch. Runs as a bare UWorld with a spawned ABaseCharacter — no PIE required, since
// StartSprinting()/StopSprinting() only touch DefaultWalkSpeed/SprintSpeed (both already valid straight
// out of the constructor) and GetCharacterMovement() (a native subobject, valid immediately after
// SpawnActor()); the one Tick() call below (for the mid-sprint-crouch case) is invoked directly rather
// than relying on a running world tick loop.
#include "Misc/AutomationTest.h"
#include "BaseCharacter.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSprintSystemTest, "MyCharacters.Movement.SprintSystem", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FSprintSystemTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("World"), World))
	{
		return false;
	}

	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);

	ABaseCharacter* Character = World->SpawnActor<ABaseCharacter>();
	bool bPassed = TestNotNull(TEXT("Character"), Character);

	if (bPassed)
	{
		UCharacterMovementComponent* MoveComp = Character->GetCharacterMovement();
		bPassed &= TestNotNull(TEXT("MovementComponent"), MoveComp);

		if (bPassed)
		{
			// 1. Default speeds set by the constructor: 300 walking, 150 crouched.
			bPassed &= TestEqual(TEXT("Default MaxWalkSpeed"), MoveComp->MaxWalkSpeed, 300.0f);
			bPassed &= TestEqual(TEXT("Default MaxWalkSpeedCrouched"), MoveComp->MaxWalkSpeedCrouched, 150.0f);
			bPassed &= TestFalse(TEXT("Not sprinting by default"), Character->bIsSprinting);

			// 2. ToggleSprint() (the Left Alt binding) flips sprint ON: 300 -> 600.
			Character->ToggleSprint();
			bPassed &= TestTrue(TEXT("bIsSprinting after first ToggleSprint"), Character->bIsSprinting);
			bPassed &= TestEqual(TEXT("MaxWalkSpeed after first ToggleSprint"), MoveComp->MaxWalkSpeed, 600.0f);

			// 3. A second ToggleSprint() (simulating the next Left Alt press) flips it back OFF: 600 -> 300.
			Character->ToggleSprint();
			bPassed &= TestFalse(TEXT("bIsSprinting after second ToggleSprint"), Character->bIsSprinting);
			bPassed &= TestEqual(TEXT("MaxWalkSpeed after second ToggleSprint"), MoveComp->MaxWalkSpeed, 300.0f);

			// 4. Sprint is blocked outright while crouched: SetIsCrouched() flips the same bIsCrouched flag
			// IsCrouched() reads, without needing a full physics-simulated crouch (no ground/movement mode
			// requirements) — ToggleSprint() must no-op (StartSprinting()'s own IsCrouched() guard) and
			// leave MaxWalkSpeed untouched.
			Character->SetIsCrouched(true);
			Character->ToggleSprint();
			bPassed &= TestFalse(TEXT("Sprint blocked while crouched"), Character->bIsSprinting);
			bPassed &= TestEqual(TEXT("MaxWalkSpeed unchanged when sprint blocked by crouch"), MoveComp->MaxWalkSpeed, 300.0f);

			// 5. Uncrouching lifts the block — toggling sprint back on should work again.
			Character->SetIsCrouched(false);
			Character->ToggleSprint();
			bPassed &= TestTrue(TEXT("bIsSprinting after uncrouching"), Character->bIsSprinting);
			bPassed &= TestEqual(TEXT("MaxWalkSpeed sprinting after uncrouching"), MoveComp->MaxWalkSpeed, 600.0f);

			// 6. A crouch that starts MID-SPRINT (not from a fresh ToggleSprint() press) auto-cancels the
			// sprint — Tick()'s own bIsSprinting && IsCrouched() check, exercised here by calling Tick()
			// directly since this bare world has no running tick loop. MaxWalkSpeed goes back to the
			// un-crouched 300 baseline; the character's REAL movement speed while crouched still comes out
			// to MaxWalkSpeedCrouched (150) regardless, via GetCharacterMovement()->GetMaxSpeed() — that
			// engine behavior isn't re-tested here since it isn't code this feature added.
			Character->SetIsCrouched(true);
			Character->Tick(0.016f);
			bPassed &= TestFalse(TEXT("bIsSprinting auto-cancelled by mid-sprint crouch"), Character->bIsSprinting);
			bPassed &= TestEqual(TEXT("MaxWalkSpeed restored by mid-sprint crouch cancel"), MoveComp->MaxWalkSpeed, 300.0f);

			Character->SetIsCrouched(false);
		}

		Character->Destroy();
	}

	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);

	return bPassed;
}

#endif // WITH_DEV_AUTOMATION_TESTS

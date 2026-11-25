// Automated coverage for task 04, step 4.3a (decision 13, user-approved change of the stable mounting mechanic):
// UMountRiderComponent mounts without the crosshair. Bare world, the Rider has no PlayerController, so
// TraceAimTarget() finds nothing — exactly the prologue's AI Rider:
//
//   (a) TryMount() mounts the nearest mountable monster within MountDistanceThreshold.
//   (b) A monster that cannot be mounted (bCanBeMounted off) is skipped for the next nearest one.
//   (c) Nothing within MountDistanceThreshold: FindMonsterToMount() is nullptr and TryMount() does nothing.
//   (d) MountMonster() on a monster out of reach refuses (CanMountMonster()'s distance check still applies).
#include "Misc/AutomationTest.h"
#include "BaseMonster.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "MountRiderComponent.h"
#include "RiderCharacter.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMountWithoutAimTest, "MyCharacters.Mounting.WithoutAim", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FMountWithoutAimTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("World"), World))
	{
		return false;
	}
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);

	// A dismounted monster goes back to its own AI and starts wandering at once; in this world without a navigation
	// system it logs that as an Error, which would fail the test by itself (same as PrologueMonsterGroupsTest).
	AddExpectedErrorPlain(TEXT("cannot wander"), EAutomationExpectedErrorFlags::Contains, -1);

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	ARiderCharacter* Rider = World->SpawnActor<ARiderCharacter>(FVector::ZeroVector, FRotator::ZeroRotator, Params);
	ABaseMonster* Near = World->SpawnActor<ABaseMonster>(FVector(300.0f, 0.0f, 0.0f), FRotator::ZeroRotator, Params);
	ABaseMonster* Second = World->SpawnActor<ABaseMonster>(FVector(0.0f, -400.0f, 0.0f), FRotator::ZeroRotator, Params);
	ABaseMonster* Far = World->SpawnActor<ABaseMonster>(FVector(-1500.0f, 0.0f, 0.0f), FRotator::ZeroRotator, Params);
	bool bPassed = TestNotNull(TEXT("Rider"), Rider) && TestNotNull(TEXT("Near"), Near) && TestNotNull(TEXT("Second"), Second) && TestNotNull(TEXT("Far"), Far);
	UMountRiderComponent* Mount = Rider ? Rider->FindComponentByClass<UMountRiderComponent>() : nullptr;
	bPassed &= TestNotNull(TEXT("Rider has a UMountRiderComponent"), Mount);
	if (!bPassed)
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return false;
	}
	// A bare world never runs BeginPlay by itself (see BullsChargeComboTest): the mount component takes its owner there.
	Rider->DispatchBeginPlay();

	// (a) The nearest one, no crosshair needed.
	bPassed &= TestTrue(TEXT("(a) nearest mountable monster picked"), Mount->FindMonsterToMount() == Near);
	Mount->TryMount();
	bPassed &= TestTrue(TEXT("(a) mounted"), Mount->bIsMounted);
	bPassed &= TestTrue(TEXT("(a) on the nearest monster"), Mount->MountedMonster == Near);
	bPassed &= TestTrue(TEXT("(a) the monster knows it is ridden"), Near->bIsBeingRidden);

	// (b) The nearest one refuses: the next one.
	Mount->Dismount();
	bPassed &= TestFalse(TEXT("(b) dismounted"), Mount->bIsMounted);
	Rider->SetActorLocation(FVector::ZeroVector);
	Near->bCanBeMounted = false;
	bPassed &= TestTrue(TEXT("(b) unmountable nearest monster skipped"), Mount->FindMonsterToMount() == Second);

	// (c) Nothing in reach.
	Second->bCanBeMounted = false;
	bPassed &= TestTrue(TEXT("(c) nothing within reach"), Mount->FindMonsterToMount() == nullptr);
	Mount->TryMount();
	bPassed &= TestFalse(TEXT("(c) TryMount() does nothing"), Mount->bIsMounted);

	// (d) A given monster out of reach is refused.
	bPassed &= TestFalse(TEXT("(d) MountMonster() refuses a monster out of reach"), Mount->MountMonster(Far));
	bPassed &= TestFalse(TEXT("(d) not mounted"), Mount->bIsMounted);

	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return bPassed;
}

#endif // WITH_DEV_AUTOMATION_TESTS

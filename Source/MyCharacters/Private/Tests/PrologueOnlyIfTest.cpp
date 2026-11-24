// Automated coverage for task 04, step 4.1: the "Binoculars used" trigger and the "Only if" condition every prologue
// action has (UPrologueAction::OnlyIf / ShouldRun()). Bare world, no level:
//
//   (a) "Binoculars used" is not met before the binoculars are used, met after LookThroughBinoculars().
//   (b) BinocularsTag: binoculars without the tag do not count, with the tag they do.
//   (c) An action without OnlyIf always runs; with OnlyIf it runs only while the condition is met, and bInvert on the
//       condition turns that around (the "walked off without the binoculars" branch of the same step).
#include "Misc/AutomationTest.h"
#include "BinocularsPickup.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "PrologueAction.h"
#include "PrologueDirector.h"
#include "PrologueTrigger.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPrologueOnlyIfTest, "MyCharacters.Prologue.OnlyIfAndBinoculars", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPrologueOnlyIfTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("World"), World))
	{
		return false;
	}
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	APrologueDirector* Director = World->SpawnActor<APrologueDirector>(FVector::ZeroVector, FRotator::ZeroRotator, Params);
	ABinocularsPickup* Binoculars = World->SpawnActor<ABinocularsPickup>(FVector(500.0f, 0.0f, 0.0f), FRotator::ZeroRotator, Params);
	bool bPassed = TestNotNull(TEXT("Director"), Director) && TestNotNull(TEXT("Binoculars"), Binoculars);
	if (!bPassed)
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return false;
	}

	UPrologueTrigger_BinocularsUsed* AnyUsed = NewObject<UPrologueTrigger_BinocularsUsed>();
	UPrologueTrigger_BinocularsUsed* TaggedUsed = NewObject<UPrologueTrigger_BinocularsUsed>();
	TaggedUsed->BinocularsTag = TEXT("Viewpoint");
	UPrologueTrigger_BinocularsUsed* NotUsed = NewObject<UPrologueTrigger_BinocularsUsed>();
	NotUsed->bInvert = true;

	UPrologueAction_Message* Always = NewObject<UPrologueAction_Message>();
	UPrologueAction_Message* OnlyIfUsed = NewObject<UPrologueAction_Message>();
	OnlyIfUsed->OnlyIf = AnyUsed;
	UPrologueAction_Message* OnlyIfNotUsed = NewObject<UPrologueAction_Message>();
	OnlyIfNotUsed->OnlyIf = NotUsed;

	// (a) + (c) before the binoculars are used.
	bPassed &= TestFalse(TEXT("(a) not met before use"), AnyUsed->IsMet(*Director));
	bPassed &= TestTrue(TEXT("(c) action without OnlyIf runs"), Always->ShouldRun(*Director));
	bPassed &= TestFalse(TEXT("(c) 'only if used' skipped before use"), OnlyIfUsed->ShouldRun(*Director));
	bPassed &= TestTrue(TEXT("(c) 'only if NOT used' runs before use"), OnlyIfNotUsed->ShouldRun(*Director));

	Binoculars->LookThroughBinoculars();

	// (a) + (b) + (c) after.
	bPassed &= TestTrue(TEXT("(a) met after use"), AnyUsed->IsMet(*Director));
	bPassed &= TestFalse(TEXT("(b) binoculars without the tag do not count"), TaggedUsed->IsMet(*Director));
	Binoculars->Tags.Add(TEXT("Viewpoint"));
	bPassed &= TestTrue(TEXT("(b) binoculars with the tag count"), TaggedUsed->IsMet(*Director));
	bPassed &= TestTrue(TEXT("(c) 'only if used' runs after use"), OnlyIfUsed->ShouldRun(*Director));
	bPassed &= TestFalse(TEXT("(c) 'only if NOT used' skipped after use"), OnlyIfNotUsed->ShouldRun(*Director));
	bPassed &= TestTrue(TEXT("(c) action without OnlyIf still runs"), Always->ShouldRun(*Director));

	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return bPassed;
}

#endif // WITH_DEV_AUTOMATION_TESTS

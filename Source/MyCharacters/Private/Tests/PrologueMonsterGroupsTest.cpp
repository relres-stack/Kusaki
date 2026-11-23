// Automated coverage for the prologue's monster groups (task 04, step 2.8a): APrologueDirector::SelectMonsterGroup() /
// SetMonsterGroupRole(). Bare world without a navmesh, so only the bookkeeping is checked here, not the movement
// (that is checked in PIE):
//
//   (a) Selection: nearest first, MaxCount, Radius, MonsterTag.
//   (b) A role switches the members' own AI off (SetScriptControlled); an empty role gives it back, the group keeps
//       its members.
//   (c) bSkipOtherGroups: a monster already in another group is not picked; without it the monster moves over and
//       leaves the first group.
//   (d) Picking the group again: previous members not picked again go back to their own AI.
#include "Misc/AutomationTest.h"
#include "BaseMonster.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "PrologueDirector.h"
#include "PrologueMonsterRole.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPrologueMonsterGroupsTest, "MyCharacters.Prologue.MonsterGroups", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPrologueMonsterGroupsTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("World"), World))
	{
		return false;
	}
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);

	bool bPassed = true;

	// A monster given back to its own AI starts patrolling/wandering at once (ABaseMonster::SetScriptControlled(false)),
	// and in this world without a navigation system it logs that as an Error, which would fail the test by itself.
	AddExpectedErrorPlain(TEXT("cannot wander"), EAutomationExpectedErrorFlags::Contains, -1);

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	// No heroes in this world: the selection counts from the director, which stands at the origin.
	APrologueDirector* Director = World->SpawnActor<APrologueDirector>(FVector::ZeroVector, FRotator::ZeroRotator, Params);
	ABaseMonster* Near = World->SpawnActor<ABaseMonster>(FVector(300.0f, 0.0f, 0.0f), FRotator::ZeroRotator, Params);
	ABaseMonster* Middle = World->SpawnActor<ABaseMonster>(FVector(0.0f, 600.0f, 0.0f), FRotator::ZeroRotator, Params);
	ABaseMonster* Far = World->SpawnActor<ABaseMonster>(FVector(-3000.0f, 0.0f, 0.0f), FRotator::ZeroRotator, Params);
	bPassed &= TestNotNull(TEXT("Director"), Director) && TestNotNull(TEXT("Near"), Near) && TestNotNull(TEXT("Middle"), Middle) && TestNotNull(TEXT("Far"), Far);
	if (!bPassed)
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return false;
	}
	Middle->Tags.Add(TEXT("Gate"));

	UPrologueMonsterRole_Surround* Surround = NewObject<UPrologueMonsterRole_Surround>();

	// (a) Selection.
	{
		FPrologueMonsterSelection Selection;
		Selection.Radius = 1000.0f;
		Selection.MaxCount = 1;
		bPassed &= TestEqual(TEXT("(a) MaxCount 1 picks one"), Director->SelectMonsterGroup(TEXT("A"), Selection), 1);
		bPassed &= TestTrue(TEXT("(a) ... the nearest"), Director->GetMonsterGroup(TEXT("A")).Contains(Near));

		Selection.MaxCount = 0;
		bPassed &= TestEqual(TEXT("(a) Radius 10 m leaves the far one out"), Director->SelectMonsterGroup(TEXT("A"), Selection), 2);
		bPassed &= TestFalse(TEXT("(a) ... far one not in it"), Director->GetMonsterGroup(TEXT("A")).Contains(Far));

		Selection.MonsterTag = TEXT("Gate");
		Selection.Radius = 0.0f;
		bPassed &= TestEqual(TEXT("(a) Tag 'Gate' anywhere picks one"), Director->SelectMonsterGroup(TEXT("A"), Selection), 1);
		bPassed &= TestTrue(TEXT("(a) ... the tagged one"), Director->GetMonsterGroup(TEXT("A")).Contains(Middle));
	}

	// (b) Role on and off.
	{
		Director->SetMonsterGroupRole(TEXT("A"), Surround, 0.0f, false);
		bPassed &= TestTrue(TEXT("(b) Role switches its own AI off"), Middle->IsScriptControlled());
		bPassed &= TestFalse(TEXT("(b) A monster outside the group keeps its AI"), Near->IsScriptControlled());

		Director->SetMonsterGroupRole(TEXT("A"), nullptr, 0.0f, false);
		bPassed &= TestFalse(TEXT("(b) Empty role gives its AI back"), Middle->IsScriptControlled());
		bPassed &= TestTrue(TEXT("(b) ... the group keeps its member"), Director->GetMonsterGroup(TEXT("A")).Contains(Middle));
		bPassed &= TestEqual(TEXT("(b) FindMonsterGroupOf"), Director->FindMonsterGroupOf(Middle), FName(TEXT("A")));
	}

	// (c) Other groups.
	{
		Director->SetMonsterGroupRole(TEXT("A"), Surround, 0.0f, false);

		FPrologueMonsterSelection Everyone;
		Everyone.Radius = 0.0f;
		bPassed &= TestEqual(TEXT("(c) Group B skips A's monster"), Director->SelectMonsterGroup(TEXT("B"), Everyone), 2);
		bPassed &= TestFalse(TEXT("(c) ... Middle not in B"), Director->GetMonsterGroup(TEXT("B")).Contains(Middle));

		Everyone.bSkipOtherGroups = false;
		bPassed &= TestEqual(TEXT("(c) Without the skip B takes all three"), Director->SelectMonsterGroup(TEXT("B"), Everyone), 3);
		bPassed &= TestEqual(TEXT("(c) ... and Middle left A"), Director->GetMonsterGroup(TEXT("A")).Num(), 0);
		bPassed &= TestEqual(TEXT("(c) FindMonsterGroupOf after the move"), Director->FindMonsterGroupOf(Middle), FName(TEXT("B")));
	}

	// (d) Re-picking a group with a role releases the monsters it drops.
	{
		Director->SetMonsterGroupRole(TEXT("B"), Surround, 0.0f, false);
		bPassed &= TestTrue(TEXT("(d) Far is driven by B's role"), Far->IsScriptControlled());

		FPrologueMonsterSelection NearOnly;
		NearOnly.Radius = 1000.0f;
		Director->SelectMonsterGroup(TEXT("B"), NearOnly);
		bPassed &= TestFalse(TEXT("(d) Far dropped from B - its own AI again"), Far->IsScriptControlled());
		bPassed &= TestTrue(TEXT("(d) Near stays in B under the role"), Near->IsScriptControlled());
	}

	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return bPassed;
}

#endif // WITH_DEV_AUTOMATION_TESTS

// Automated coverage for step 4.8 of task 04 (the way to the fence): which way a hero faces when "Move to" walks him
// backwards or toward a target (UHeroScriptCommand_MoveTo::ComputeFacingYaw), and where "Place monsters at marker" puts
// the monsters (UPrologueAction_PlaceMonsters::RingPoint). Pure math — no world, no navmesh.
// A bug here would turn the Priest's face to the fence instead of the monsters, or drop the crowd in one heap.
#include "Misc/AutomationTest.h"
#include "HeroScriptCommand.h"
#include "PrologueAction.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPrologueEscortTest, "MyCharacters.Prologue.Escort", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPrologueEscortTest::RunTest(const FString& Parameters)
{
	const FVector Hero(100.0f, 200.0f, 90.0f);
	float Yaw = 0.0f;

	// (a) Backwards along +Y: he faces -Y (yaw -90).
	TestTrue(TEXT("(a) backwards has a direction"), UHeroScriptCommand_MoveTo::ComputeFacingYaw(EHeroScriptMoveFacing::Backwards, Hero, FVector(0.0f, 1.0f, 0.0f), FVector::ZeroVector, Yaw));
	TestTrue(TEXT("(a) backwards along +Y faces -Y"), FMath::IsNearlyEqual(FMath::FindDeltaAngleDegrees(Yaw, -90.0f), 0.0f, 0.5f));

	// (b) The vertical part of the move does not tilt the facing (walking down a slope backwards).
	TestTrue(TEXT("(b) slope"), UHeroScriptCommand_MoveTo::ComputeFacingYaw(EHeroScriptMoveFacing::Backwards, Hero, FVector(1.0f, 0.0f, -0.5f), FVector::ZeroVector, Yaw));
	TestTrue(TEXT("(b) backwards along +X (sloping) faces -X"), FMath::IsNearlyEqual(FMath::Abs(FMath::FindDeltaAngleDegrees(Yaw, 180.0f)), 0.0f, 0.5f));

	// (c) Not moving yet (no direction): no facing — he keeps his own.
	TestFalse(TEXT("(c) no direction"), UHeroScriptCommand_MoveTo::ComputeFacingYaw(EHeroScriptMoveFacing::Backwards, Hero, FVector::ZeroVector, FVector::ZeroVector, Yaw));

	// (d) Toward a target: straight at it, height ignored.
	TestTrue(TEXT("(d) target"), UHeroScriptCommand_MoveTo::ComputeFacingYaw(EHeroScriptMoveFacing::Target, Hero, FVector(0.0f, 1.0f, 0.0f), Hero + FVector(-300.0f, 0.0f, 500.0f), Yaw));
	TestTrue(TEXT("(d) faces the target (-X)"), FMath::IsNearlyEqual(FMath::Abs(FMath::FindDeltaAngleDegrees(Yaw, 180.0f)), 0.0f, 0.5f));

	// (e) Normal walking: the command does not turn him.
	TestFalse(TEXT("(e) forward = no override"), UHeroScriptCommand_MoveTo::ComputeFacingYaw(EHeroScriptMoveFacing::Forward, Hero, FVector(0.0f, 1.0f, 0.0f), FVector::ZeroVector, Yaw));

	// (f) Ring: 4 monsters 400 cm around the centre, the first one where the marker faces (+X), then every 90 deg.
	{
		const FVector Center(1000.0f, -500.0f, 30.0f);
		const FVector P0 = UPrologueAction_PlaceMonsters::RingPoint(Center, 0.0f, 400.0f, 0, 4);
		const FVector P1 = UPrologueAction_PlaceMonsters::RingPoint(Center, 0.0f, 400.0f, 1, 4);
		const FVector P2 = UPrologueAction_PlaceMonsters::RingPoint(Center, 0.0f, 400.0f, 2, 4);
		TestTrue(TEXT("(f) first on +X"), P0.Equals(Center + FVector(400.0f, 0.0f, 0.0f), 1.0f));
		TestTrue(TEXT("(f) second on +Y"), P1.Equals(Center + FVector(0.0f, 400.0f, 0.0f), 1.0f));
		TestTrue(TEXT("(f) third on -X"), P2.Equals(Center + FVector(-400.0f, 0.0f, 0.0f), 1.0f));
		TestTrue(TEXT("(f) all at the radius"), FMath::IsNearlyEqual(FVector::Dist2D(P1, Center), 400.0f, 1.0f));
	}

	// (g) No ring (radius 0) or a single monster: on the marker itself.
	{
		const FVector Center(10.0f, 20.0f, 30.0f);
		TestTrue(TEXT("(g) radius 0 = centre"), UPrologueAction_PlaceMonsters::RingPoint(Center, 45.0f, 0.0f, 3, 5).Equals(Center, 0.1f));
		TestTrue(TEXT("(g) one monster in a ring = on the ring at the marker's facing"),
			UPrologueAction_PlaceMonsters::RingPoint(Center, 90.0f, 200.0f, 0, 1).Equals(Center + FVector(0.0f, 200.0f, 0.0f), 1.0f));
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

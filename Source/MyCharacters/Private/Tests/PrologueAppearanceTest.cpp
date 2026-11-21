// Automated coverage for choosing where a prologue hero appears (task 04, step 2.2; see PrologueAppearance.h).
// Pure math — no world: the player looks from the origin along +X, candidate points are made up, and visibility
// is a lambda instead of a line trace. A bug here would put a hero behind a wall or behind the player's back.
#include "Misc/AutomationTest.h"
#include "PrologueAppearance.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	// Point at a horizontal distance and angle (degrees, + = to the left of +X) from the origin.
	FVector PointAt(float Distance, float AngleDeg, float Z = 0.0f)
	{
		const float Rad = FMath::DegreesToRadians(AngleDeg);
		return FVector(FMath::Cos(Rad) * Distance, FMath::Sin(Rad) * Distance, Z);
	}

	const FVector ViewLocation(0.0f, 0.0f, 170.0f);
	const FVector ViewDir(1.0f, 0.0f, -0.2f);   // looking slightly down: the angle must still be measured flat
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPrologueAppearanceTest, "MyCharacters.Prologue.AppearancePoint", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPrologueAppearanceTest::RunTest(const FString& Parameters)
{
	// Defaults: distance 8-40 m (prefer 15 m), angle up to 50 deg (prefer 20 deg).
	const FPrologueAppearancePlacement Placement;
	auto AllVisible = [](const FVector&) { return true; };

	// (a) The point nearest the preferred distance and angle wins, whatever its place in the list.
	{
		const TArray<FVector> Points = { PointAt(3000.0f, 20.0f), PointAt(1500.0f, 0.0f), PointAt(1500.0f, 20.0f) };
		TestEqual(TEXT("(a) preferred distance+angle wins"),
			PrologueAppearance::ChooseBestPoint(Points, ViewLocation, ViewDir, Placement, AllVisible), 2);
	}

	// (b) Behind the player or wider than MaxViewAngle: never used.
	{
		const TArray<FVector> Points = { PointAt(1500.0f, 180.0f), PointAt(1500.0f, -90.0f), PointAt(1500.0f, 60.0f) };
		TestEqual(TEXT("(b) out of view -> none"),
			PrologueAppearance::ChooseBestPoint(Points, ViewLocation, ViewDir, Placement, AllVisible), (int32)INDEX_NONE);
	}

	// (c) Too close or too far: never used.
	{
		const TArray<FVector> Points = { PointAt(500.0f, 20.0f), PointAt(5000.0f, 20.0f) };
		TestEqual(TEXT("(c) out of distance -> none"),
			PrologueAppearance::ChooseBestPoint(Points, ViewLocation, ViewDir, Placement, AllVisible), (int32)INDEX_NONE);
	}

	// (d) The best point is hidden (behind a wall): the next best visible one is used.
	{
		const TArray<FVector> Points = { PointAt(1500.0f, 20.0f), PointAt(1500.0f, 0.0f), PointAt(3000.0f, 20.0f) };
		const FVector Hidden = Points[0];
		auto HideFirst = [&Hidden](const FVector& P) { return !P.Equals(Hidden, 1.0f); };
		// Scores: [1] = 20/50 = 0.4, [2] = 1500/3200 = 0.47 -> [1].
		TestEqual(TEXT("(d) hidden best -> next best visible"),
			PrologueAppearance::ChooseBestPoint(Points, ViewLocation, ViewDir, Placement, HideFirst), 1);
	}

	// (e) Nothing visible / nothing at all.
	{
		const TArray<FVector> Points = { PointAt(1500.0f, 20.0f) };
		TestEqual(TEXT("(e) all hidden -> none"),
			PrologueAppearance::ChooseBestPoint(Points, ViewLocation, ViewDir, Placement, [](const FVector&) { return false; }), (int32)INDEX_NONE);
		TestEqual(TEXT("(e) empty list -> none"),
			PrologueAppearance::ChooseBestPoint(TArray<FVector>(), ViewLocation, ViewDir, Placement, AllVisible), (int32)INDEX_NONE);
	}

	// (f) Height does not matter: a point on a slope far above or below is judged by its flat distance and angle.
	{
		TestTrue(TEXT("(f) angle is flat"), FMath::IsNearlyEqual(PrologueAppearance::ViewAngleTo(ViewLocation, ViewDir, PointAt(1500.0f, 0.0f, 2000.0f)), 0.0f, 0.01f));
		const TArray<FVector> Points = { PointAt(1500.0f, 20.0f, -800.0f) };
		TestEqual(TEXT("(f) low point still allowed"),
			PrologueAppearance::ChooseBestPoint(Points, ViewLocation, ViewDir, Placement, AllVisible), 0);
	}

	// (g) Visibility is asked only for allowed points (no wasted line traces on points behind the player).
	{
		const TArray<FVector> Points = { PointAt(1500.0f, 180.0f), PointAt(9000.0f, 0.0f), PointAt(1500.0f, 20.0f) };
		int32 Asked = 0;
		auto Count = [&Asked](const FVector&) { ++Asked; return true; };
		PrologueAppearance::ChooseBestPoint(Points, ViewLocation, ViewDir, Placement, Count);
		TestEqual(TEXT("(g) one visibility check"), Asked, 1);
	}

	// (h) Generated points (no candidate points in the level) all lie within the limits, and the best of them is
	// the preferred distance and angle.
	{
		const TArray<FVector> Generated = PrologueAppearance::MakeFallbackCandidates(ViewLocation, ViewDir, Placement);
		TestTrue(TEXT("(h) enough generated points"), Generated.Num() >= 9);
		bool bAllInside = true;
		for (const FVector& P : Generated)
		{
			bAllInside &= PrologueAppearance::IsWithinLimits(ViewLocation, ViewDir, P, Placement);
		}
		TestTrue(TEXT("(h) all generated points within limits"), bAllInside);

		const int32 Best = PrologueAppearance::ChooseBestPoint(Generated, ViewLocation, ViewDir, Placement, AllVisible);
		if (TestTrue(TEXT("(h) a generated point is chosen"), Generated.IsValidIndex(Best)))
		{
			TestTrue(TEXT("(h) chosen at the preferred distance"), FMath::IsNearlyEqual(FVector::Dist2D(ViewLocation, Generated[Best]), Placement.PreferredDistance, 1.0f));
			TestTrue(TEXT("(h) chosen at the preferred angle"), FMath::IsNearlyEqual(PrologueAppearance::ViewAngleTo(ViewLocation, ViewDir, Generated[Best]), Placement.PreferredViewAngle, 0.1f));
		}
	}

	// (i) Broken data (Min > Max, preferred outside the limits) does not break the choice.
	{
		FPrologueAppearancePlacement Broken;
		Broken.MinDistance = 4000.0f;
		Broken.MaxDistance = 800.0f;
		Broken.PreferredDistance = 99999.0f;
		Broken.PreferredViewAngle = 90.0f;   // wider than MaxViewAngle (50)
		const TArray<FVector> Points = { PointAt(1500.0f, 0.0f), PointAt(4000.0f, 50.0f) };
		// Sane limits: 8-40 m, prefer 40 m and 50 deg -> the far wide point.
		TestEqual(TEXT("(i) broken limits are sanitised"),
			PrologueAppearance::ChooseBestPoint(Points, ViewLocation, ViewDir, Broken, AllVisible), 1);
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

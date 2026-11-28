// Automated coverage for the "Hero sees hero" trigger (task 04, step 4.6; see UPrologueTrigger_HeroSeesHero).
// Pure math — no world: the view and the target point are made up. The line-of-sight trace is checked in PIE.
// A bug here would switch control to the Rider while the lying Priest looks the other way, or never switch at all.
#include "Misc/AutomationTest.h"
#include "PrologueTrigger.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	// The Priest lying on the road: eyes 30 cm above the ground; the Rider on the fence 27 m away, 7 m up.
	const FVector Eyes(0.0f, 0.0f, 30.0f);
	const FVector Rider(2630.0f, 550.0f, 740.0f);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPrologueHeroSeesHeroTest, "MyCharacters.Prologue.HeroSeesHero", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPrologueHeroSeesHeroTest::RunTest(const FString& Parameters)
{
	const FVector AtRider = Rider - Eyes;

	// (a) Looking straight at her: seen, with any angle limit.
	TestTrue(TEXT("(a) looking straight at her"), UPrologueTrigger_HeroSeesHero::IsPointInView(Eyes, AtRider, Rider, 5.0f, 0.0f));

	// (b) The view direction need not be normalised.
	TestTrue(TEXT("(b) unnormalised direction"), UPrologueTrigger_HeroSeesHero::IsPointInView(Eyes, AtRider * 37.0f, Rider, 5.0f, 0.0f));

	// (c) Looking 20 deg to the side: seen with a 30 deg limit, not with a 12 deg one.
	{
		const FVector Side = FRotator(0.0f, 20.0f, 0.0f).RotateVector(AtRider);
		TestTrue(TEXT("(c) 20 deg off, limit 30"), UPrologueTrigger_HeroSeesHero::IsPointInView(Eyes, Side, Rider, 30.0f, 0.0f));
		TestFalse(TEXT("(c) 20 deg off, limit 12"), UPrologueTrigger_HeroSeesHero::IsPointInView(Eyes, Side, Rider, 12.0f, 0.0f));
	}

	// (d) Looking up at the sky above her (lying on his back): the vertical angle counts too, not only the flat one.
	{
		const FVector Up = FRotator(40.0f, 0.0f, 0.0f).RotateVector(AtRider.GetSafeNormal2D());
		TestFalse(TEXT("(d) looking 40 deg up"), UPrologueTrigger_HeroSeesHero::IsPointInView(Eyes, Up, Rider, 12.0f, 0.0f));
	}

	// (e) Looking away (behind): never seen, even with the widest limit.
	TestFalse(TEXT("(e) looking away"), UPrologueTrigger_HeroSeesHero::IsPointInView(Eyes, -AtRider, Rider, 90.0f, 0.0f));

	// (f) Distance limit: 27 m away is seen within 30 m, not within 20 m; 0 = any distance.
	TestTrue(TEXT("(f) within 30 m"), UPrologueTrigger_HeroSeesHero::IsPointInView(Eyes, AtRider, Rider, 12.0f, 3000.0f));
	TestFalse(TEXT("(f) beyond 20 m"), UPrologueTrigger_HeroSeesHero::IsPointInView(Eyes, AtRider, Rider, 12.0f, 2000.0f));

	// (g) No view direction at all: not seen (no crash).
	TestFalse(TEXT("(g) zero direction"), UPrologueTrigger_HeroSeesHero::IsPointInView(Eyes, FVector::ZeroVector, Rider, 90.0f, 0.0f));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

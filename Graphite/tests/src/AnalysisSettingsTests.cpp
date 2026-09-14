#include <doctest/doctest.h>

#include "Settings/AnalysisSettings.h"

// None of these call Load(). AppSettings writes beside the test executable, so a Load() here
// would read whatever an earlier case's setter left behind and make the suite order-dependent.
// The setters below do still write a real graphite.json next to the test executable — that is
// deliberate and harmless, since nothing in this suite reads it back, and the directory is
// disposable build output.
TEST_CASE("The budget is offered in megabytes and used in bytes")
{
	Graphite::AnalysisSettings settings;
	settings.SetBudgetMegabytes(64);

	CHECK(settings.GetBudgetMegabytes() == 64);
	CHECK(settings.GetBudgetBytes() == 64ull * 1024 * 1024);
}

TEST_CASE("A budget outside what the panel offers is clamped rather than accepted")
{
	Graphite::AnalysisSettings settings;

	settings.SetBudgetMegabytes(0);
	CHECK(settings.GetBudgetMegabytes() == Graphite::AnalysisSettings::kMinimumBudgetMegabytes);

	settings.SetBudgetMegabytes(999999);
	CHECK(settings.GetBudgetMegabytes() == Graphite::AnalysisSettings::kMaximumBudgetMegabytes);
}

TEST_CASE("Background analysis is on until it is turned off")
{
	Graphite::AnalysisSettings settings;

	CHECK(settings.IsEnabled());

	settings.SetEnabled(false);

	CHECK_FALSE(settings.IsEnabled());
}

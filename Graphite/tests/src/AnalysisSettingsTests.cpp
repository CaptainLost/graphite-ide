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

TEST_CASE("The automatic concurrency is derived from the machine and stays in its bounds")
{
	const std::size_t automatic = Graphite::AnalysisSettings::AutomaticConcurrency();

	CHECK(automatic >= 2);
	CHECK(automatic <= 6);
}

TEST_CASE("A concurrency outside what the panel offers is clamped rather than accepted")
{
	Graphite::AnalysisSettings settings;

	settings.SetConcurrencyAutomatic(false);

	settings.SetConcurrency(0);
	CHECK(settings.GetConcurrency() == Graphite::AnalysisSettings::kMinimumConcurrency);

	settings.SetConcurrency(999);
	CHECK(settings.GetConcurrency() == Graphite::AnalysisSettings::kMaximumConcurrency);
}

TEST_CASE("Concurrency is automatic until somebody says otherwise")
{
	Graphite::AnalysisSettings settings;

	CHECK(settings.IsConcurrencyAutomatic());
	CHECK(settings.GetConcurrency() == Graphite::AnalysisSettings::AutomaticConcurrency());

	settings.SetConcurrencyAutomatic(false);
	settings.SetConcurrency(2);

	CHECK_FALSE(settings.IsConcurrencyAutomatic());
	CHECK(settings.GetConcurrency() == 2);

	// Back to automatic, and the machine's answer returns rather than the number that was typed.
	settings.SetConcurrencyAutomatic(true);

	CHECK(settings.GetConcurrency() == Graphite::AnalysisSettings::AutomaticConcurrency());
}

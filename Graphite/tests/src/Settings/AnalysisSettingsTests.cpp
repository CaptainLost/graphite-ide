#include <doctest/doctest.h>

#include "Settings/AnalysisSettings.h"

#include "TempAppSettings.h"

// Every case gets its own AppSettings over a temporary file (TempAppSettings), so a case starts
// from the defaults whatever an earlier one set, and the user's own settings are never touched.
TEST_CASE("The budget is offered in megabytes and used in bytes")
{
	TempAppSettings store;
	Graphite::AnalysisSettings settings{store.Get()};
	settings.SetBudgetMegabytes(64);

	CHECK(settings.GetBudgetMegabytes() == 64);
	CHECK(settings.GetBudgetBytes() == 64ull * 1024 * 1024);
}

TEST_CASE("A budget outside what the panel offers is clamped rather than accepted")
{
	TempAppSettings store;
	Graphite::AnalysisSettings settings{store.Get()};

	settings.SetBudgetMegabytes(0);
	CHECK(settings.GetBudgetMegabytes() == Graphite::AnalysisSettings::kMinimumBudgetMegabytes);

	settings.SetBudgetMegabytes(999999);
	CHECK(settings.GetBudgetMegabytes() == Graphite::AnalysisSettings::kMaximumBudgetMegabytes);
}

TEST_CASE("Background analysis is on until it is turned off")
{
	TempAppSettings store;
	Graphite::AnalysisSettings settings{store.Get()};

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
	TempAppSettings store;
	Graphite::AnalysisSettings settings{store.Get()};

	settings.SetConcurrencyAutomatic(false);

	settings.SetConcurrency(0);
	CHECK(settings.GetConcurrency() == Graphite::AnalysisSettings::kMinimumConcurrency);

	settings.SetConcurrency(999);
	CHECK(settings.GetConcurrency() == Graphite::AnalysisSettings::kMaximumConcurrency);
}

TEST_CASE("Concurrency is automatic until somebody says otherwise")
{
	TempAppSettings store;
	Graphite::AnalysisSettings settings{store.Get()};

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

TEST_CASE("Unticking automatic keeps the figure it had rather than dropping to the minimum")
{
	TempAppSettings store;
	Graphite::AnalysisSettings settings{store.Get()};

	CHECK(settings.IsConcurrencyAutomatic());

	settings.SetConcurrencyAutomatic(false);

	// Nothing was dragged, so this should read as "still what automatic chose", not as
	// having quietly fallen back to kMinimumConcurrency.
	CHECK(settings.GetConcurrency() == Graphite::AnalysisSettings::AutomaticConcurrency());
}

TEST_CASE("A round trip through automatic returns the number that was chosen, not the machine's")
{
	TempAppSettings store;
	Graphite::AnalysisSettings settings{store.Get()};

	settings.SetConcurrencyAutomatic(false);
	settings.SetConcurrency(2);

	settings.SetConcurrencyAutomatic(true);
	settings.SetConcurrencyAutomatic(false);

	// The deliberate 2 comes back, not whatever AutomaticConcurrency() happens to say.
	CHECK(settings.GetConcurrency() == 2);
}

TEST_CASE("While automatic is on, the remembered choice does not leak into what is reported")
{
	TempAppSettings store;
	Graphite::AnalysisSettings settings{store.Get()};

	settings.SetConcurrencyAutomatic(false);
	settings.SetConcurrency(2);

	settings.SetConcurrencyAutomatic(true);

	// Automatic means automatic: the number on screen is the machine's, not the one that was
	// typed before the box was ticked back on.
	CHECK(settings.GetConcurrency() == Graphite::AnalysisSettings::AutomaticConcurrency());
}

TEST_CASE("AnalysisSettings loads back what it set, after a restart")
{
	TempAppSettings store;

	{
		Graphite::AnalysisSettings settings{store.Get()};
		settings.SetEnabled(false);
		settings.SetBudgetMegabytes(64);
		settings.SetConcurrency(3);
	}

	Graphite::AnalysisSettings reloaded{store.Reload()};
	reloaded.Load();

	CHECK_FALSE(reloaded.IsEnabled());
	CHECK(reloaded.GetBudgetMegabytes() == 64);
	CHECK_FALSE(reloaded.IsConcurrencyAutomatic());
	CHECK(reloaded.GetConcurrency() == 3);
}

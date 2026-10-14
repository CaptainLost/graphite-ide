#include <doctest/doctest.h>

#include "Settings/AnalysisSettings.h"

#include "TempAppSettings.h"

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

	settings.SetConcurrencyAutomatic(true);

	CHECK(settings.GetConcurrency() == Graphite::AnalysisSettings::AutomaticConcurrency());
}

TEST_CASE("Unticking automatic keeps the figure it had rather than dropping to the minimum")
{
	TempAppSettings store;
	Graphite::AnalysisSettings settings{store.Get()};

	CHECK(settings.IsConcurrencyAutomatic());

	settings.SetConcurrencyAutomatic(false);

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

	CHECK(settings.GetConcurrency() == 2);
}

TEST_CASE("While automatic is on, the remembered choice does not leak into what is reported")
{
	TempAppSettings store;
	Graphite::AnalysisSettings settings{store.Get()};

	settings.SetConcurrencyAutomatic(false);
	settings.SetConcurrency(2);

	settings.SetConcurrencyAutomatic(true);

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

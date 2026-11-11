#include <doctest/doctest.h>

#include "Sessions/ClaudeSettings.h"

#include "TempAppSettings.h"

using namespace Graphite;

TEST_CASE("A price list kept in settings still gets every default it does not override")
{
	TempAppSettings store;
	store.Get().SetClaudePrice("claude-opus-5", "1,2,3,4,5");

	ClaudeSettings settings{store.Reload()};
	settings.Load();

	REQUIRE(settings.GetPricing().Find("claude-opus-5") != nullptr);
	CHECK(settings.GetPricing().Find("claude-opus-5")->Input == doctest::Approx(1.0));

	for (const char* model : {"claude-sonnet-5-5", "claude-haiku-5-5", "claude-fable-5-2"})
	{
		CAPTURE(model);
		CHECK(settings.GetPricing().Find(model) != nullptr);
	}
}

TEST_CASE("Fetched prices and the day they were fetched are kept apart from edited prices across a restart")
{
	TempAppSettings store;

	{
		ClaudeSettings settings{store.Get()};
		settings.Load();
		CHECK(settings.GetPricesFetchedOn().empty());

		settings.AdoptFetchedPrices({{"claude-opus-4-5", StandardPrice(5.0, 25.0, 0.5)}, {"claude-opus-5", StandardPrice(3.0, 15.0, 0.3)}},
									"2026-11-11");
		settings.SetPrice("claude-opus-5", StandardPrice(1.0, 2.0, 0.1));
	}

	ClaudeSettings reloaded{store.Reload()};
	reloaded.Load();

	CHECK(reloaded.GetPricesFetchedOn() == "2026-11-11");
	CHECK(reloaded.GetPricing().SourceOf("claude-opus-4-5") == PriceSource::Fetched);
	CHECK(reloaded.GetPricing().SourceOf("claude-opus-5") == PriceSource::Edited);

	reloaded.ResetPrice("claude-opus-5");
	CHECK(reloaded.GetPricing().Find("claude-opus-5")->Input == doctest::Approx(3.0));
}

TEST_CASE("A newer fetch replaces every price the older one kept")
{
	TempAppSettings store;

	{
		ClaudeSettings settings{store.Get()};
		settings.Load();
		settings.AdoptFetchedPrices({{"claude-opus-4-5", StandardPrice(5.0, 25.0, 0.5)}}, "2026-11-10");
		settings.AdoptFetchedPrices({{"claude-opus-4-1", StandardPrice(15.0, 75.0, 1.5)}}, "2026-11-11");
	}

	ClaudeSettings reloaded{store.Reload()};
	reloaded.Load();

	CHECK(reloaded.GetPricing().Find("claude-opus-4-5") == nullptr);
	CHECK(reloaded.GetPricing().Find("claude-opus-4-1") != nullptr);
	CHECK(reloaded.GetPricesFetchedOn() == "2026-11-11");
}

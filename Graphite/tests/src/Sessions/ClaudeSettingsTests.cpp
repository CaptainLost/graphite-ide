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

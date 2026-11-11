#include <doctest/doctest.h>

#include "Sessions/ClaudeSettings.h"
#include "Sessions/PriceUpdates.h"
#include "Sessions/PricingPage.h"

#include "FakeHttpClient.h"
#include "TempAppSettings.h"

#include <functional>
#include <string>

using namespace Graphite;

namespace {

	const std::string kPage = "## Model pricing\n\n"
							  "| Model | Base input tokens | 5m cache writes | 1h cache writes | Cache hits and refreshes | Output tokens |\n"
							  "| :--- | :--- | :--- | :--- | :--- | :--- |\n"
							  "| Claude Opus 4.5 | $5 / MTok | $6.25 / MTok | $10 / MTok | $0.50 / MTok | $25 / MTok |\n"
							  "| Claude Opus 5 | $3 / MTok | $3.75 / MTok | $6 / MTok | $0.30 / MTok | $15 / MTok |\n";

	struct Fixture
	{
		TempAppSettings Store;
		ClaudeSettings Settings{Store.Get()};
		Core::Ref<FakeHttpClient> Http = Core::CreateRef<FakeHttpClient>();
		PriceUpdates Updates{Settings, Http, [](std::function<void()> job) { job(); }, [] { return std::string{"2026-11-11"}; }};

		Fixture() { Settings.Load(); }

		void AnswerWithPage() { Http->Answer(std::string{kPricingPageUrl}, Core::kHttpStatusOk, kPage); }
	};
}

TEST_CASE("Prices never fetched are fetched at start, and a fetch lands only when polled")
{
	Fixture fixture;
	fixture.AnswerWithPage();

	fixture.Updates.FetchIfNeverFetched();

	CHECK(fixture.Http->Requests.size() == 1);
	CHECK(fixture.Settings.GetPricing().Find("claude-opus-4-5") == nullptr);

	fixture.Updates.Poll();

	CHECK(fixture.Settings.GetPricing().SourceOf("claude-opus-4-5") == PriceSource::Fetched);
	CHECK(fixture.Settings.GetPricing().Find("claude-opus-5")->Input == doctest::Approx(3.0));
	CHECK(fixture.Settings.GetPricesFetchedOn() == "2026-11-11");
	CHECK_FALSE(fixture.Updates.GetProblem().has_value());
	CHECK_FALSE(fixture.Updates.IsFetching());
}

TEST_CASE("Prices fetched before are not fetched again at start")
{
	Fixture fixture;
	fixture.Settings.AdoptFetchedPrices({}, "2026-11-01");

	fixture.Updates.FetchIfNeverFetched();

	CHECK(fixture.Http->Requests.empty());
}

TEST_CASE("A model with no price asks for a fetch once a run, however many times it is seen")
{
	Fixture fixture;
	fixture.Settings.AdoptFetchedPrices({}, "2026-11-01");
	fixture.AnswerWithPage();

	fixture.Updates.FetchForUnpricedModel();
	fixture.Updates.Poll();
	fixture.Updates.FetchForUnpricedModel();
	fixture.Updates.Poll();

	CHECK(fixture.Http->Requests.size() == 1);
}

TEST_CASE("A model with no price asks for nothing when the page was fetched today")
{
	Fixture fixture;
	fixture.Settings.AdoptFetchedPrices({}, "2026-11-11");
	fixture.AnswerWithPage();

	fixture.Updates.FetchForUnpricedModel();

	CHECK(fixture.Http->Requests.empty());
}

TEST_CASE("A model with no price asks for nothing once the start of the run has fetched")
{
	Fixture fixture;
	fixture.AnswerWithPage();

	fixture.Updates.FetchIfNeverFetched();
	fixture.Updates.Poll();
	fixture.Updates.FetchForUnpricedModel();

	CHECK(fixture.Http->Requests.size() == 1);
}

TEST_CASE("Asking by hand fetches every time")
{
	Fixture fixture;
	fixture.AnswerWithPage();

	fixture.Updates.Fetch();
	fixture.Updates.Poll();
	fixture.Updates.Fetch();
	fixture.Updates.Poll();

	CHECK(fixture.Http->Requests.size() == 2);
}

TEST_CASE("A failed fetch keeps the prices there were and says why")
{
	Fixture fixture;
	fixture.Settings.AdoptFetchedPrices({{"claude-opus-4-5", StandardPrice(5.0, 25.0, 0.5)}}, "2026-11-01");

	fixture.Updates.Fetch();
	fixture.Updates.Poll();

	REQUIRE(fixture.Updates.GetProblem().has_value());
	CHECK(fixture.Settings.GetPricing().SourceOf("claude-opus-4-5") == PriceSource::Fetched);
	CHECK(fixture.Settings.GetPricesFetchedOn() == "2026-11-01");
}

TEST_CASE("A page whose table is not recognised keeps the prices there were and says why")
{
	Fixture fixture;
	fixture.Http->Answer(std::string{kPricingPageUrl}, Core::kHttpStatusOk, "# Pricing\n\nMoved.\n");

	fixture.Updates.Fetch();
	fixture.Updates.Poll();

	REQUIRE(fixture.Updates.GetProblem().has_value());
	CHECK(fixture.Settings.GetPricesFetchedOn().empty());
	CHECK(fixture.Settings.GetPricing().SourceOf("claude-opus-5") == PriceSource::BuiltIn);
}

TEST_CASE("A refused fetch is a problem, and a later good one clears it")
{
	Fixture fixture;
	fixture.Http->Answer(std::string{kPricingPageUrl}, 503, "");

	fixture.Updates.Fetch();
	fixture.Updates.Poll();
	REQUIRE(fixture.Updates.GetProblem().has_value());

	fixture.AnswerWithPage();
	fixture.Updates.Fetch();
	fixture.Updates.Poll();
	CHECK_FALSE(fixture.Updates.GetProblem().has_value());
}

TEST_CASE("With no way to reach the network nothing is fetched and nothing breaks")
{
	TempAppSettings store;
	ClaudeSettings settings{store.Get()};
	settings.Load();
	PriceUpdates updates{settings, nullptr, [](std::function<void()> job) { job(); }, [] { return std::string{"2026-11-11"}; }};

	CHECK_FALSE(updates.CanFetch());

	updates.FetchIfNeverFetched();
	updates.Fetch();
	updates.Poll();

	CHECK_FALSE(updates.IsFetching());
	CHECK(settings.GetPricesFetchedOn().empty());
}

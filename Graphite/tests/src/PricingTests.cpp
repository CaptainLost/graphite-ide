#include <doctest/doctest.h>

#include "Sessions/Pricing.h"
#include "Sessions/RequestTimeline.h"

using namespace Graphite;

namespace {
	constexpr std::uint64_t kMillion = 1'000'000;
}

TEST_CASE("A million tokens in each bucket of Opus 5 costs the list price of each")
{
	const Pricing pricing = Pricing::Defaults();
	const UsageByModel usage{{{"claude-opus-5", false}, {kMillion, kMillion, kMillion, kMillion, kMillion}}};

	const CostResult cost = pricing.CostOf(usage);

	REQUIRE(cost.IsKnown());
	// 5 input + 25 output + 6.25 five-minute write + 10 hour write + 0.5 read.
	CHECK(cost.Dollars == doctest::Approx(46.75));
}

TEST_CASE("Opus 5.5 is not priced as Opus 5, although one id begins with the other")
{
	const Pricing pricing = Pricing::Defaults();

	REQUIRE(pricing.Find("claude-opus-5-5") != nullptr);
	CHECK(pricing.Find("claude-opus-5-5")->Input == doctest::Approx(4.0));
	CHECK(pricing.Find("claude-opus-5")->Input == doctest::Approx(5.0));
}

TEST_CASE("A dated or bracketed id is priced as the model it names")
{
	const Pricing pricing = Pricing::Defaults();

	REQUIRE(pricing.Find("claude-haiku-4-5-20251001") != nullptr);
	CHECK(pricing.Find("claude-haiku-4-5-20251001")->Input == doctest::Approx(1.0));
	REQUIRE(pricing.Find("claude-opus-5[1m]") != nullptr);
}

TEST_CASE("An unknown model is unknown, never free — even one whose id extends a known one")
{
	const Pricing pricing = Pricing::Defaults();

	CHECK(pricing.Find("claude-opus-5-7") == nullptr);

	const CostResult cost =
		pricing.CostOf({{{"claude-opus-5-7", false}, {10, 10, 0, 0, 0}},
						{{"claude-opus-5", false}, {kMillion, 0, 0, 0, 0}}});

	CHECK_FALSE(cost.IsKnown());
	CHECK(cost.UnknownModels == std::vector<std::string>{"claude-opus-5-7"});
	CHECK(cost.Dollars == doctest::Approx(5.0));
}

TEST_CASE("An unknown model with no tokens does not make the cost unknown")
{
	const CostResult cost = Pricing::Defaults().CostOf({{{"mystery", false}, {}}});

	CHECK(cost.IsKnown());
}

TEST_CASE("Messages Claude Code writes itself are known and free")
{
	const CostResult cost = Pricing::Defaults().CostOf({{{"<synthetic>", false}, {kMillion, kMillion, 0, 0, 0}}});

	CHECK(cost.IsKnown());
	CHECK(cost.Dollars == doctest::Approx(0.0));
}

TEST_CASE("A fast request costs twice the standard rate")
{
	const Pricing pricing = Pricing::Defaults();

	const double standard = pricing.CostOf({{{"claude-opus-5", false}, {kMillion, kMillion, 0, 0, 0}}}).Dollars;
	const double fast = pricing.CostOf({{{"claude-opus-5", true}, {kMillion, kMillion, 0, 0, 0}}}).Dollars;

	CHECK(fast == doctest::Approx(2.0 * standard));
}

TEST_CASE("An override replaces the default and a reset brings it back")
{
	Pricing pricing = Pricing::Defaults();

	pricing.Set("claude-opus-5", StandardPrice(1.0, 2.0, 0.1));
	CHECK(pricing.Find("claude-opus-5")->Input == doctest::Approx(1.0));

	pricing.Reset("claude-opus-5");
	CHECK(pricing.Find("claude-opus-5")->Input == doctest::Approx(5.0));
}

TEST_CASE("A price reads back from the text it is stored as")
{
	const Price price = StandardPrice(5.0, 25.0, 0.5);

	CHECK(ParsePrice(FormatPrice(price)) == price);
	CHECK_FALSE(ParsePrice("5,25,6.25,10").has_value());
	CHECK_FALSE(ParsePrice("5,25,x,10,0.5").has_value());
	CHECK_FALSE(ParsePrice("5,25,6.25,10,-1").has_value());
}

TEST_CASE("Bucket costs add up to the cost of the same usage")
{
	const Pricing pricing = Pricing::Defaults();
	const TokenCounts counts{1000, 2000, 3000, 4000, 5000};

	const std::optional<BucketCosts> buckets = pricing.BucketsOf({"claude-sonnet-5", false}, counts);

	REQUIRE(buckets.has_value());
	CHECK(buckets->Total() == doctest::Approx(pricing.CostOf({{{"claude-sonnet-5", false}, counts}}).Dollars));
}

TEST_CASE("A request is priced as its usage under its model and speed")
{
	const Pricing pricing = Pricing::Defaults();
	ApiRequest request;
	request.Model = "claude-opus-5";
	request.Usage = {kMillion, kMillion, 0, 0, 0};

	CHECK(pricing.CostOf(request).Dollars ==
		  doctest::Approx(pricing.CostOf({{{"claude-opus-5", false}, request.Usage}}).Dollars));

	request.Fast = true;
	CHECK(pricing.CostOf(request).Dollars ==
		  doctest::Approx(pricing.CostOf({{{"claude-opus-5", true}, request.Usage}}).Dollars));

	request.Model = "mystery";
	CHECK(pricing.CostOf(request).UnknownModels == std::vector<std::string>{"mystery"});
}

TEST_CASE("Adding a cost adds the dollars and lists each unknown model once")
{
	CostResult total{1.5, {"a"}};

	total += CostResult{2.0, {"b", "a"}};

	CHECK(total.Dollars == doctest::Approx(3.5));
	CHECK(total.UnknownModels == std::vector<std::string>{"a", "b"});
	CHECK(CostResult{}.IsKnown());
}

TEST_CASE("A model's label drops the claude prefix and marks a fast request")
{
	CHECK(ModelLabel("claude-opus-5-5", false) == "opus-5-5");
	CHECK(ModelLabel("claude-opus-5-5", true) == "opus-5-5 fast");
	CHECK(ModelLabel("mystery", false) == "mystery");
	CHECK(ModelLabel("", false).empty());
}

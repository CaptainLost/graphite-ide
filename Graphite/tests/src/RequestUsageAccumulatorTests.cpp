#include <doctest/doctest.h>

#include "Sessions/JsonAccess.h"
#include "Sessions/RequestUsageAccumulator.h"

using namespace Graphite;

TEST_CASE("The first usage a request sees is adopted and added to the total")
{
	RequestUsageAccumulator request;
	TokenCounts total{1, 1, 0, 0, 0};

	CHECK_FALSE(request.HasUsage());
	CHECK(request.Add({10, 5, 0, 0, 100}, &total));

	CHECK(request.HasUsage());
	CHECK(request.Counts() == TokenCounts{10, 5, 0, 0, 100});
	CHECK(total == TokenCounts{11, 6, 0, 0, 100});
}

TEST_CASE("A later record keeps the larger count per bucket and corrects the total by the difference")
{
	RequestUsageAccumulator request;
	TokenCounts total;

	request.Add({10, 2, 0, 0, 100}, &total);
	CHECK(request.Add({10, 30, 0, 0, 90}, &total));

	CHECK(request.Counts() == TokenCounts{10, 30, 0, 0, 100});
	CHECK(total == TokenCounts{10, 30, 0, 0, 100});
}

TEST_CASE("A record that adds nothing reports no change and leaves the total alone")
{
	RequestUsageAccumulator request;
	TokenCounts total;

	request.Add({10, 30, 0, 0, 100}, &total);
	CHECK_FALSE(request.Add({10, 30, 0, 0, 100}, &total));
	CHECK_FALSE(request.Add({1, 1, 0, 0, 1}, &total));

	CHECK(total == TokenCounts{10, 30, 0, 0, 100});
}

TEST_CASE("An accumulator resumed from counts merges into them without touching a total it was not given")
{
	RequestUsageAccumulator request{TokenCounts{4, 4, 0, 0, 0}};

	CHECK(request.HasUsage());
	CHECK(request.Add({1, 9, 0, 0, 0}));
	CHECK(request.Counts() == TokenCounts{4, 9, 0, 0, 0});
}

TEST_CASE("A request whose first usage is all zero still counts as having usage")
{
	RequestUsageAccumulator request;

	CHECK(request.Add({}));
	CHECK(request.HasUsage());
	CHECK(request.Add({0, 3, 0, 0, 0}));
	CHECK(request.Counts().Output == 3);
}

TEST_CASE("Count accessors read integers and give a missing, negative or mistyped field nothing")
{
	const nlohmann::json object = nlohmann::json::parse(R"({"a":7,"b":-3,"c":"x","d":0})");

	CHECK(OptionalCountAt(object, "a") == 7);
	CHECK(OptionalCountAt(object, "d") == 0);
	CHECK_FALSE(OptionalCountAt(object, "b").has_value());
	CHECK_FALSE(OptionalCountAt(object, "c").has_value());
	CHECK_FALSE(OptionalCountAt(object, "missing").has_value());
	CHECK(CountAt(object, "a") == 7);
	CHECK(CountAt(object, "b") == 0);
	CHECK(StringAt(object, "c") == "x");
	CHECK(StringAt(object, "a").empty());
	CHECK(ObjectOf("[1]").empty());
	CHECK(ObjectOf("not json").is_object());
	CHECK(ObjectOf(R"({"k":1})").contains("k"));
}

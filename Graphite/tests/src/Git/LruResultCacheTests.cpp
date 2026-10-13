#include <doctest/doctest.h>

#include "Git/LruResultCache.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

using Graphite::LruResultCache;

namespace {

	using Cache = LruResultCache<int, std::string>;

	std::shared_ptr<const std::string> Text(const char* text)
	{
		return std::make_shared<const std::string>(text);
	}

	// Stated as a bool so a failed check prints rather than trying to stream a pointer.
	bool Holds(const Cache& cache, int key)
	{
		return cache.Get(key) != nullptr;
	}

	// Asks for a key and lands a value under the generation the ask handed out.
	void Fill(Cache& cache, int key, const char* text)
	{
		const auto generation = cache.Request(key);
		REQUIRE(generation.has_value());
		REQUIRE(cache.Succeed(key, *generation, Text(text)));
	}
}

TEST_CASE("A cache hands out a generation for a key it does not hold")
{
	Cache cache(4);

	const auto generation = cache.Request(1);

	CHECK(generation.has_value());
	CHECK_FALSE(Holds(cache, 1));
	CHECK(cache.GetSize() == 1);
}

TEST_CASE("A cache does not ask again for a key that is in flight, held or failed")
{
	Cache cache(4);

	const auto inFlight = cache.Request(1);
	REQUIRE(inFlight.has_value());
	CHECK_FALSE(cache.Request(1).has_value());

	REQUIRE(cache.Succeed(1, *inFlight, Text("one")));
	CHECK_FALSE(cache.Request(1).has_value());
	CHECK(*cache.Get(1) == "one");

	const auto failing = cache.Request(2);
	REQUIRE(failing.has_value());
	REQUIRE(cache.Fail(2, *failing));
	CHECK(cache.IsFailed(2));
	CHECK_FALSE(cache.Request(2).has_value());
}

TEST_CASE("A cache drops a landing for a key it no longer holds")
{
	Cache cache(4);

	const auto generation = cache.Request(1);
	REQUIRE(generation.has_value());
	cache.Clear();

	CHECK_FALSE(cache.Succeed(1, *generation, Text("one")));
	CHECK_FALSE(cache.Fail(1, *generation));
	CHECK(cache.GetSize() == 0);
}

TEST_CASE("A landing from before a clear cannot outrank one from after it")
{
	Cache cache(4);

	const auto before = cache.Request(1);
	REQUIRE(before.has_value());
	cache.Clear();

	const auto after = cache.Request(1);
	REQUIRE(after.has_value());
	CHECK(*after > *before);

	REQUIRE(cache.Succeed(1, *after, Text("after")));
	CHECK_FALSE(cache.Succeed(1, *before, Text("before")));
	CHECK(*cache.Get(1) == "after");
}

TEST_CASE("A full cache evicts the entry touched longest ago")
{
	Cache cache(3);
	Fill(cache, 1, "one");
	Fill(cache, 2, "two");
	Fill(cache, 3, "three");

	// Reading counts as a use, so the oldest by touch is now 2.
	CHECK(Holds(cache, 1));

	Fill(cache, 4, "four");

	CHECK(cache.GetSize() == 3);
	CHECK(Holds(cache, 1));
	CHECK_FALSE(Holds(cache, 2));
	CHECK(Holds(cache, 3));
	CHECK(Holds(cache, 4));
}

TEST_CASE("Asking again for a held key counts as a use for eviction")
{
	Cache cache(2);
	Fill(cache, 1, "one");
	Fill(cache, 2, "two");

	CHECK_FALSE(cache.Request(1).has_value());

	Fill(cache, 3, "three");

	CHECK(Holds(cache, 1));
	CHECK_FALSE(Holds(cache, 2));
}

TEST_CASE("A full cache never evicts an entry that is in flight")
{
	Cache cache(2);

	const auto first = cache.Request(1);
	const auto second = cache.Request(2);
	REQUIRE(first.has_value());
	REQUIRE(second.has_value());

	// Nothing is evictable, so the cache grows past its bound rather than dropping a
	// question whose answer is still coming.
	const auto third = cache.Request(3);
	REQUIRE(third.has_value());
	CHECK(cache.GetSize() == 3);

	REQUIRE(cache.Succeed(1, *first, Text("one")));
	REQUIRE(cache.Succeed(2, *second, Text("two")));
	REQUIRE(cache.Succeed(3, *third, Text("three")));

	// Once they have landed, the next new key brings the cache back under its bound.
	CHECK(cache.Request(4).has_value());
	CHECK(cache.GetSize() == 2);
}

TEST_CASE("Forgetting a failed entry lets it be asked for again, and leaves a held one alone")
{
	Cache cache(4);
	Fill(cache, 1, "one");

	const auto failing = cache.Request(2);
	REQUIRE(failing.has_value());
	REQUIRE(cache.Fail(2, *failing));

	CHECK_FALSE(cache.ForgetFailed(1));
	CHECK(Holds(cache, 1));

	CHECK(cache.ForgetFailed(2));
	CHECK_FALSE(cache.IsFailed(2));
	CHECK(cache.Request(2).has_value());

	CHECK_FALSE(cache.ForgetFailed(3));
}

namespace {

	bool IsEven(int key)
	{
		return key % 2 == 0;
	}

	void FailKey(Cache& cache, int key)
	{
		const std::optional<std::uint64_t> generation = cache.Request(key);
		REQUIRE(generation.has_value());
		REQUIRE(cache.Fail(key, *generation));
	}
}

TEST_CASE("Forgetting the failed entries a test accepts leaves the others and every held one alone")
{
	Cache cache(8);
	Fill(cache, 2, "two");
	FailKey(cache, 3);
	FailKey(cache, 4);
	FailKey(cache, 6);

	CHECK(cache.ForgetFailedWhere(IsEven) == 2);

	CHECK(Holds(cache, 2));
	CHECK(cache.IsFailed(3));
	CHECK_FALSE(cache.IsFailed(4));
	CHECK_FALSE(cache.IsFailed(6));
	CHECK(cache.Request(4).has_value());
}

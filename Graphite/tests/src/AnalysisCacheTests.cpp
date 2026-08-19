#include <doctest/doctest.h>

#include "Editor/AnalysisCache.h"

#include <chrono>

namespace {

	std::filesystem::file_time_type At(int seconds)
	{
		return std::filesystem::file_time_type{} + std::chrono::seconds(seconds);
	}

	Graphite::CachedAnalysis WithCount(std::string name, std::size_t count, int modifiedAt)
	{
		Graphite::CachedAnalysis analysis;
		analysis.Counts.push_back({std::move(name), Graphite::DeclarationKind::Function, count});
		analysis.ModifiedAt = At(modifiedAt);

		return analysis;
	}
}

TEST_CASE("A document's analysis comes back exactly as it was left")
{
	Graphite::AnalysisCache cache{4};
	cache.Put("a.cpp", WithCount("Draw", 7, 100));

	const std::optional<Graphite::CachedAnalysis> restored = cache.Take("a.cpp", At(100));

	REQUIRE(restored.has_value());
	REQUIRE(restored->Counts.size() == 1);
	CHECK(restored->Counts[0].Name == "Draw");
	CHECK(restored->Counts[0].Count == 7);
}

TEST_CASE("A document nothing was ever kept for restores nothing")
{
	Graphite::AnalysisCache cache{4};

	CHECK_FALSE(cache.Take("never-seen.cpp", At(100)).has_value());
}

TEST_CASE("Analysis of a file that has changed since is not restored")
{
	Graphite::AnalysisCache cache{4};
	cache.Put("a.cpp", WithCount("Draw", 7, 100));

	CHECK_FALSE(cache.Take("a.cpp", At(200)).has_value());
}

TEST_CASE("Analysis rejected as out of date is discarded rather than kept")
{
	Graphite::AnalysisCache cache{4};
	cache.Put("a.cpp", WithCount("Draw", 7, 100));

	cache.Take("a.cpp", At(200));

	CHECK_FALSE(cache.Take("a.cpp", At(100)).has_value());
}

TEST_CASE("Restoring a document consumes what was kept for it")
{
	Graphite::AnalysisCache cache{4};
	cache.Put("a.cpp", WithCount("Draw", 7, 100));

	REQUIRE(cache.Take("a.cpp", At(100)).has_value());

	CHECK_FALSE(cache.Take("a.cpp", At(100)).has_value());
}

TEST_CASE("Keeping more documents than there is room for forgets the least recently kept")
{
	Graphite::AnalysisCache cache{2};
	cache.Put("first.cpp", WithCount("A", 1, 100));
	cache.Put("second.cpp", WithCount("B", 2, 100));
	cache.Put("third.cpp", WithCount("C", 3, 100));

	CHECK_FALSE(cache.Take("first.cpp", At(100)).has_value());
	CHECK(cache.Take("second.cpp", At(100)).has_value());
	CHECK(cache.Take("third.cpp", At(100)).has_value());
}

TEST_CASE("Keeping a document again moves it out of the way of the next eviction")
{
	Graphite::AnalysisCache cache{2};
	cache.Put("first.cpp", WithCount("A", 1, 100));
	cache.Put("second.cpp", WithCount("B", 2, 100));
	cache.Put("first.cpp", WithCount("A", 9, 100));
	cache.Put("third.cpp", WithCount("C", 3, 100));

	CHECK_FALSE(cache.Take("second.cpp", At(100)).has_value());

	const std::optional<Graphite::CachedAnalysis> restored = cache.Take("first.cpp", At(100));

	REQUIRE(restored.has_value());
	CHECK(restored->Counts[0].Count == 9);
}

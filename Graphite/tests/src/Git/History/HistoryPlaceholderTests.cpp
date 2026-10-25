#include <doctest/doctest.h>

#include "Git/History/HistoryPlaceholder.h"

#include <optional>

namespace {
	const Core::Error kNoRepository{Core::ErrorCode::NotFound, "no repository is open"};
	const Core::Error kLogFailed{Core::ErrorCode::IoFailure, "object not found - no match for id"};
	constexpr bool kStatusDelivered = true;
	constexpr bool kNoStatusDelivered = false;
}

TEST_CASE("The history says it is loading while no answer has come back")
{
	CHECK_FALSE(Graphite::HistoryPlaceholder(kNoStatusDelivered, std::nullopt, std::nullopt).has_value());
}

TEST_CASE("A folder that is not a repository says so rather than loading forever")
{
	CHECK(Graphite::HistoryPlaceholder(kNoStatusDelivered, kNoRepository, kNoRepository) == "No repository.");
	CHECK(Graphite::HistoryPlaceholder(kNoStatusDelivered, kNoRepository, std::nullopt) == "No repository.");
}

TEST_CASE("A failed history query says it failed and why, rather than claiming there is no repository")
{
	CHECK(Graphite::HistoryPlaceholder(kStatusDelivered, std::nullopt, kLogFailed) ==
		  "Could not load the history: object not found - no match for id");
}

TEST_CASE("A status that failed for another reason is not read as a missing repository")
{
	const Core::Error statusFailed{Core::ErrorCode::IoFailure, "the index is locked"};

	CHECK_FALSE(Graphite::HistoryPlaceholder(kNoStatusDelivered, statusFailed, std::nullopt).has_value());
	CHECK(Graphite::HistoryPlaceholder(kNoStatusDelivered, statusFailed, kLogFailed) ==
		  "Could not load the history: object not found - no match for id");
}

TEST_CASE("A folder is missing a repository only when the status said so and no status was ever delivered")
{
	CHECK(Graphite::IsMissingRepository(kNoStatusDelivered, kNoRepository));

	CHECK_FALSE(Graphite::IsMissingRepository(kNoStatusDelivered, std::nullopt));

	CHECK_FALSE(Graphite::IsMissingRepository(
		kNoStatusDelivered, Core::Error{Core::ErrorCode::IoFailure, "the index is locked"}));

	CHECK_FALSE(Graphite::IsMissingRepository(kStatusDelivered, kNoRepository));
}

TEST_CASE("While a new attempt at the history runs an earlier failure gives way to the loader")
{
	CHECK_FALSE(
		Graphite::HistoryPlaceholder(kStatusDelivered, std::nullopt, kLogFailed, Graphite::LogAttempt::InFlight).has_value());
}

TEST_CASE("A missing repository is still said while a new attempt at the history runs")
{
	CHECK(Graphite::HistoryPlaceholder(kNoStatusDelivered, kNoRepository, kLogFailed, Graphite::LogAttempt::InFlight) ==
		  "No repository.");
}
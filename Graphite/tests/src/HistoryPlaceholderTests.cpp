#include <doctest/doctest.h>

#include "Git/History/HistoryPlaceholder.h"

#include <optional>

namespace {
	const Core::Error kNoRepository{Core::ErrorCode::NotFound, "no repository is open"};
	const Core::Error kLogFailed{Core::ErrorCode::IoFailure, "object not found - no match for id"};
}

TEST_CASE("The history says it is loading while no answer has come back")
{
	CHECK(Graphite::HistoryPlaceholder(false, std::nullopt, std::nullopt) == "Loading history…");
}

TEST_CASE("A folder that is not a repository says so rather than loading forever")
{
	CHECK(Graphite::HistoryPlaceholder(false, kNoRepository, kNoRepository) == "No repository.");
	CHECK(Graphite::HistoryPlaceholder(false, kNoRepository, std::nullopt) == "No repository.");
}

// A failed query is a failure, and saying "No repository." about a repository whose status
// query merely failed would send the reader looking for a problem they do not have.
TEST_CASE("A failed history query says it failed and why")
{
	CHECK(Graphite::HistoryPlaceholder(true, std::nullopt, kLogFailed) ==
		  "Could not load the history: object not found - no match for id");
}

TEST_CASE("A status that failed for another reason is not read as a missing repository")
{
	const Core::Error statusFailed{Core::ErrorCode::IoFailure, "the index is locked"};

	CHECK(Graphite::HistoryPlaceholder(false, statusFailed, std::nullopt) == "Loading history…");
	CHECK(Graphite::HistoryPlaceholder(false, statusFailed, kLogFailed) ==
		  "Could not load the history: object not found - no match for id");
}

TEST_CASE("A folder is missing a repository only when the status said so and never answered")
{
	CHECK(Graphite::IsMissingRepository(false, kNoRepository));

	// Still loading: no answer either way yet, so no button may appear.
	CHECK_FALSE(Graphite::IsMissingRepository(false, std::nullopt));

	// A status that failed for another reason is a failure, not an absence.
	CHECK_FALSE(Graphite::IsMissingRepository(
		false, Core::Error{Core::ErrorCode::IoFailure, "the index is locked"}));

	// A repository answered before: the error is about this refresh, not the folder.
	CHECK_FALSE(Graphite::IsMissingRepository(true, kNoRepository));
}

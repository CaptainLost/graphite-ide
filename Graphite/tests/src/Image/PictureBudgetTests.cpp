#include <doctest/doctest.h>

#include "Image/PictureBudget.h"

#include <filesystem>
#include <set>
#include <vector>

using Graphite::PictureBudget;

namespace {

	constexpr std::uint64_t kLimit = 100;

	const std::filesystem::path kOldest = "oldest.png";
	const std::filesystem::path kMiddle = "middle.png";
	const std::filesystem::path kNewest = "newest.png";
}

TEST_CASE("The least recently shown picture is released first")
{
	PictureBudget budget(kLimit);
	budget.Note(kNewest, 40, 30);
	budget.Note(kOldest, 40, 10);
	budget.Note(kMiddle, 40, 20);

	CHECK(budget.OverBudget({}) == std::vector<std::filesystem::path>{kOldest});

	budget.Note(kNewest, 90, 30);
	CHECK(budget.OverBudget({}) == std::vector<std::filesystem::path>{kOldest, kMiddle});

	// A released picture is noted again with nothing decoded, and a closed one is forgotten.
	budget.Note(kOldest, 0, 10);
	budget.Forget(kMiddle);
	CHECK(budget.OverBudget({}).empty());
}

TEST_CASE("A picture on screen is never released, even past the budget")
{
	PictureBudget budget(kLimit);
	budget.Note(kOldest, 500, 10);
	budget.Note(kNewest, 50, 30);

	const std::set<std::filesystem::path> onScreen{kOldest};
	CHECK(budget.OverBudget(onScreen) == std::vector<std::filesystem::path>{kNewest});

	budget.Note(kNewest, 0, 30);
	CHECK(budget.OverBudget(onScreen).empty());
}

TEST_CASE("The budget defaults to one gibibyte")
{
	PictureBudget budget;
	budget.Note(kOldest, 1ull << 30, 10);
	CHECK(budget.OverBudget({}).empty());

	budget.Note(kNewest, 1, 20);
	CHECK(budget.OverBudget({}) == std::vector<std::filesystem::path>{kOldest});
}

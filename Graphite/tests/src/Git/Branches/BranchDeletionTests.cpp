#include <doctest/doctest.h>

#include "Git/Branches/BranchDeletion.h"

#include <optional>
#include <string>

namespace {

	Graphite::DeleteCheck CountedCheck(const char* branch, std::size_t unmerged)
	{
		return Graphite::DeleteCheck{.Branch = branch, .Unmerged = unmerged};
	}

	Graphite::DeleteCheck FailedCheck(const char* branch)
	{
		return Graphite::DeleteCheck{.Branch = branch, .Failure = Core::Error{Core::ErrorCode::IoFailure, "walk stopped"}};
	}
}

TEST_CASE("A delete check that fails after the reader cancelled the question is dropped, not reported")
{
	Graphite::BranchDeletion deletion;
	deletion.Begin("old");

	deletion.Finish();

	CHECK(deletion.Land(FailedCheck("old")) == Graphite::DeleteCheckLanding::Ignored);
}

TEST_CASE("A delete check that lands after the reader cancelled counts nothing")
{
	Graphite::BranchDeletion deletion;
	deletion.Begin("old");
	deletion.Finish();

	CHECK(deletion.Land(CountedCheck("old", 3)) == Graphite::DeleteCheckLanding::Ignored);
	CHECK_FALSE(deletion.UnmergedOf("old").has_value());
}

TEST_CASE("A delete check that fails while its question is open is reported and the question is forgotten")
{
	Graphite::BranchDeletion deletion;
	deletion.Begin("old");

	CHECK(deletion.Land(FailedCheck("old")) == Graphite::DeleteCheckLanding::Failed);
	CHECK_FALSE(deletion.UnansweredBranch().has_value());
}

TEST_CASE("A delete check's count is given to its own question, and only once it has landed")
{
	Graphite::BranchDeletion deletion;
	deletion.Begin("old");

	CHECK_FALSE(deletion.UnmergedOf("old").has_value());

	CHECK(deletion.Land(CountedCheck("old", 3)) == Graphite::DeleteCheckLanding::Counted);
	CHECK(deletion.UnmergedOf("old") == std::optional<std::size_t>{3});
	CHECK_FALSE(deletion.UnmergedOf("other").has_value());
}

TEST_CASE("A check for a branch other than the one asked about is ignored")
{
	Graphite::BranchDeletion deletion;
	deletion.Begin("new");

	CHECK(deletion.Land(FailedCheck("old")) == Graphite::DeleteCheckLanding::Ignored);
	CHECK(deletion.UnansweredBranch() == std::optional<std::string>{"new"});
}

TEST_CASE("A delete question held for the hidden panel survives the withdrawal and is asked again once on screen")
{
	constexpr bool kHidden = false;
	constexpr bool kOnScreen = true;

	Graphite::BranchDeletion deletion;
	deletion.Begin("old");

	deletion.HoldForPanel();
	deletion.ForgetUnlessHeld();

	CHECK(deletion.IsHeldForPanel());
	CHECK_FALSE(deletion.TakeHeldOnceOnScreen(kHidden).has_value());
	CHECK(deletion.TakeHeldOnceOnScreen(kOnScreen) == std::optional<std::string>{"old"});
	CHECK_FALSE(deletion.TakeHeldOnceOnScreen(kOnScreen).has_value());
	CHECK(deletion.UnansweredBranch() == std::optional<std::string>{"old"});
}

TEST_CASE("A delete question withdrawn without being held is forgotten")
{
	Graphite::BranchDeletion deletion;
	deletion.Begin("old");

	deletion.ForgetUnlessHeld();

	CHECK_FALSE(deletion.UnansweredBranch().has_value());
	CHECK(deletion.Land(FailedCheck("old")) == Graphite::DeleteCheckLanding::Ignored);
}

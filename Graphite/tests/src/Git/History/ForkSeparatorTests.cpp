#include <doctest/doctest.h>

#include "GitFixtures.h"

#include "Git/History/ForkSeparator.h"

#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

	Core::LogPage PageOf(std::initializer_list<std::uint8_t> firstBytes, bool complete = true)
	{
		Core::LogPage page;
		page.Complete = complete;

		for (const std::uint8_t firstByte : firstBytes)
		{
			Core::CommitSummary commit;
			commit.Id = OidWithFirstByte(firstByte);
			page.Commits.push_back(commit);
		}

		return page;
	}
}

namespace {

	constexpr bool kIsHead = true;

	Core::Branch LocalBranch(std::string name, std::uint8_t tip, bool isHead = false,
							 std::optional<std::string> upstream = std::nullopt)
	{
		Core::Branch branch;
		branch.Name = name;
		branch.FullName = "refs/heads/" + name;
		branch.Tip = OidWithFirstByte(tip);
		branch.IsHead = isHead;
		branch.Upstream = std::move(upstream);
		return branch;
	}

	Core::Branch OriginDefault(std::uint8_t tip)
	{
		Core::Branch branch;
		branch.Name = "origin/main";
		branch.FullName = "refs/remotes/origin/main";
		branch.Kind = Core::RefKind::Remote;
		branch.Tip = OidWithFirstByte(tip);
		branch.IsRemoteDefault = true;
		return branch;
	}

	std::vector<Core::Branch> Repository()
	{
		return {LocalBranch("main", 1, false, "origin/main"), LocalBranch("feature", 5, kIsHead), OriginDefault(1)};
	}
}

TEST_CASE("A selected branch is measured against the default branch")
{
	const std::optional<Graphite::ForkBase> fork = Graphite::FindForkBase("refs/heads/feature", Repository());

	REQUIRE(fork.has_value());
	CHECK(fork->BaseName == "main");
	CHECK(fork->OwnCommits.Ref == "refs/heads/feature");
	CHECK(fork->OwnCommits.Exclude == OidWithFirstByte(1));
}

TEST_CASE("Without a selected branch the checked-out one is measured")
{
	const std::optional<Graphite::ForkBase> fork = Graphite::FindForkBase(std::nullopt, Repository());

	REQUIRE(fork.has_value());
	CHECK(fork->OwnCommits.Ref == "refs/heads/feature");
}

TEST_CASE("A detached HEAD has no branch to measure")
{
	std::vector<Core::Branch> branches = Repository();
	branches[1].IsHead = false;

	CHECK_FALSE(Graphite::FindForkBase(std::nullopt, branches).has_value());
}

TEST_CASE("A selected ref that is not a branch has nothing to measure")
{
	CHECK_FALSE(Graphite::FindForkBase("refs/heads/gone", Repository()).has_value());
}

TEST_CASE("The default branch has no fork point, since its base is only its own upstream")
{
	CHECK_FALSE(Graphite::FindForkBase("refs/heads/main", Repository()).has_value());
}

TEST_CASE("The remote of the default branch has no fork point either")
{
	CHECK_FALSE(Graphite::FindForkBase("refs/remotes/origin/main", Repository()).has_value());
}

TEST_CASE("A repository that names no base gets no separator unless a branch is checked out")
{
	const std::vector<Core::Branch> branches = {LocalBranch("only", 1)};

	CHECK_FALSE(Graphite::FindForkBase("refs/heads/only", branches).has_value());
}

TEST_CASE("The separator sits before the first commit the branch does not own")
{
	const Core::LogPage page = PageOf({1, 2, 3, 4});
	const Core::LogPage own = PageOf({1, 2});

	const std::optional<Graphite::ForkSeparator> separator = Graphite::FindForkSeparator(page, own, "main");

	REQUIRE(separator.has_value());
	CHECK(separator->BeforeCommit == 2);
	CHECK(separator->Text == "Earlier history · already in main");
}

TEST_CASE("A branch with no commits of its own gets the separator on top and says so")
{
	const Core::LogPage page = PageOf({3, 4});
	const Core::LogPage own = PageOf({});

	const std::optional<Graphite::ForkSeparator> separator = Graphite::FindForkSeparator(page, own, "main");

	REQUIRE(separator.has_value());
	CHECK(separator->BeforeCommit == 0);
	CHECK(separator->Text == "No new commits · already in main");
}

TEST_CASE("A page made only of the branch's own commits has nothing to separate yet")
{
	const Core::LogPage page = PageOf({1, 2}, false);
	const Core::LogPage own = PageOf({1, 2, 3});

	CHECK_FALSE(Graphite::FindForkSeparator(page, own, "main").has_value());
}

TEST_CASE("An empty page has no separator")
{
	const Core::LogPage page = PageOf({});
	const Core::LogPage own = PageOf({});

	CHECK_FALSE(Graphite::FindForkSeparator(page, own, "main").has_value());
}

TEST_CASE("A count of own commits cut short cannot place the separator")
{
	const Core::LogPage page = PageOf({1, 2, 3});
	const Core::LogPage own = PageOf({1}, false);

	CHECK_FALSE(Graphite::FindForkSeparator(page, own, "main").has_value());
}

TEST_CASE("Commits merged in from the base come after the separator even when they are interleaved")
{
	const Core::LogPage page = PageOf({1, 9, 2, 3});
	const Core::LogPage own = PageOf({1, 2});

	const std::optional<Graphite::ForkSeparator> separator = Graphite::FindForkSeparator(page, own, "main");

	REQUIRE(separator.has_value());
	CHECK(separator->BeforeCommit == 1);
}

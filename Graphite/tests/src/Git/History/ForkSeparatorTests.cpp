#include <doctest/doctest.h>

#include "GitFixtures.h"
#include "Git/Lines/LineReadFixtures.h"

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

	// main is a base tracking origin/main; feature, checked out, is compared with main; lone is a base with no
	// upstream.
	struct Repository
	{
		Repository()
		{
			Graphite::Fixtures::LineReadBuilder builder;
			builder.AddRemote("origin");
			const std::size_t main = builder.AddLocal("main");
			const std::size_t feature = builder.AddLocal("feature", Graphite::Fixtures::BranchSpec{.Head = true});
			const std::size_t lone = builder.AddLocal("lone");
			const std::size_t originMain = builder.AddRemoteBranch("origin", "main");
			builder.MakeBase(main);
			builder.MakeBase(lone);
			builder.Track(main, originMain);
			builder.Compare(feature, main);
			Read = builder.Build();
			Lines.emplace(Read);
		}

		Graphite::LineRead Read;
		std::optional<Graphite::BranchLines> Lines;

		std::optional<Graphite::ForkBase> ForkOf(const std::optional<std::string>& selectedRef) const
		{
			return Graphite::FindForkBase(selectedRef, *Read.Branches, &*Lines);
		}
	};
}

TEST_CASE("Git window: the fork base of a branch is its line")
{
	const Repository repository;

	const std::optional<Graphite::ForkBase> fork = repository.ForkOf("refs/heads/feature");

	REQUIRE(fork.has_value());
	CHECK(fork->BaseName == "main");
	CHECK(fork->OwnCommits.Ref == "refs/heads/feature");
	CHECK(fork->OwnCommits.Exclude == (*repository.Read.Branches)[0].Tip);
}

TEST_CASE("Git window: without a selected branch the checked-out one is measured")
{
	const Repository repository;

	const std::optional<Graphite::ForkBase> fork = repository.ForkOf(std::nullopt);

	REQUIRE(fork.has_value());
	CHECK(fork->OwnCommits.Ref == "refs/heads/feature");
}

TEST_CASE("Git window: a detached HEAD has no branch to measure")
{
	Graphite::Fixtures::LineReadBuilder builder;
	const std::size_t main = builder.AddLocal("main");
	const std::size_t feature = builder.AddLocal("feature");
	builder.MakeBase(main);
	builder.Compare(feature, main);
	const Graphite::LineRead read = builder.Build();
	const Graphite::BranchLines lines(read);

	CHECK_FALSE(Graphite::FindForkBase(std::nullopt, *read.Branches, &lines).has_value());
}

TEST_CASE("Git window: a selected ref that is not a branch has nothing to measure")
{
	const Repository repository;

	CHECK_FALSE(repository.ForkOf("refs/heads/gone").has_value());
}

TEST_CASE("Git window: a base is measured against its upstream")
{
	const Repository repository;

	const std::optional<Graphite::ForkBase> fork = repository.ForkOf("refs/heads/main");

	REQUIRE(fork.has_value());
	CHECK(fork->BaseName == "origin/main");
}

TEST_CASE("Git window: the remote of a base has no fork point either")
{
	const Repository repository;

	CHECK_FALSE(repository.ForkOf("refs/remotes/origin/main").has_value());
}

TEST_CASE("Git window: a branch compared with nothing, a base with no upstream and a missing line read get no separator")
{
	const Repository repository;

	CHECK_FALSE(repository.ForkOf("refs/heads/lone").has_value());
	CHECK_FALSE(Graphite::FindForkBase("refs/heads/feature", *repository.Read.Branches, nullptr).has_value());

	Graphite::Fixtures::LineReadBuilder builder;
	builder.AddLocal("orphan");
	const Graphite::LineRead read = builder.Build();
	const Graphite::BranchLines lines(read);

	CHECK_FALSE(Graphite::FindForkBase("refs/heads/orphan", *read.Branches, &lines).has_value());
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

TEST_CASE("A branch with no commits of its own has nothing above a separator, so it gets none")
{
	const Core::LogPage page = PageOf({3, 4});
	const Core::LogPage own = PageOf({});

	CHECK_FALSE(Graphite::FindForkSeparator(page, own, "main").has_value());
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

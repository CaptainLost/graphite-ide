#include <doctest/doctest.h>

#include "Git/Lines/LineReadFixtures.h"
#include "Review/ReviewProgress.h"

#include <optional>
#include <string>
#include <vector>

using Graphite::ReviewSessions;

namespace {

	const std::string kMain = "refs/heads/main";
	const std::string kSide = "refs/heads/side";
	const std::string kNext = "refs/heads/next";

	// main is a base tracking origin/main; side is compared with main; next is compared with nothing.
	struct Repository
	{
		Repository()
		{
			Graphite::Fixtures::LineReadBuilder builder;
			builder.AddRemote("origin");
			const std::size_t main = builder.AddLocal("main");
			const std::size_t side = builder.AddLocal("side");
			builder.AddLocal("next");
			const std::size_t originMain = builder.AddRemoteBranch("origin", "main");
			builder.MakeBase(main);
			builder.Track(main, originMain);
			builder.Compare(side, main);
			Read = builder.Build();
			Lines.emplace(Read);
		}

		Graphite::LineRead Read;
		std::optional<Graphite::BranchLines> Lines;

		const std::vector<Core::Branch>& Branches() const { return *Read.Branches; }
		const Graphite::BranchLines* LinesPtr() const { return &*Lines; }
	};
}

TEST_CASE("Git window: a branch with a session or a draft is reviewed against the base of that session")
{
	const Repository repository;
	ReviewSessions sessions;

	sessions.Start(kSide, kNext);
	CHECK(Graphite::ReviewBaseOf(kSide, repository.Branches(), sessions, repository.LinesPtr()) == kNext);

	sessions.OpenDraft(kNext, kSide);
	CHECK(Graphite::ReviewBaseOf(kNext, repository.Branches(), sessions, repository.LinesPtr()) == kSide);
}

TEST_CASE("Git window: a branch without a session is reviewed against its line and a base against its upstream")
{
	const Repository repository;
	const ReviewSessions sessions;

	CHECK(Graphite::ReviewBaseOf(kSide, repository.Branches(), sessions, repository.LinesPtr()) == kMain);
	CHECK(Graphite::ReviewBaseOf(kMain, repository.Branches(), sessions, repository.LinesPtr()) ==
		  "refs/remotes/origin/main");
}

TEST_CASE("Git window: a branch compared with nothing has no review base and the message says what to do")
{
	const Repository repository;
	const ReviewSessions sessions;

	CHECK_FALSE(Graphite::ReviewBaseOf(kNext, repository.Branches(), sessions, repository.LinesPtr()).has_value());
	CHECK_FALSE(Graphite::ReviewBaseOf("refs/heads/not-read-yet", repository.Branches(), sessions, repository.LinesPtr())
					.has_value());
	CHECK(Graphite::NoReviewBaseMessage(kNext, repository.Branches(), repository.LinesPtr()) ==
		  "next is compared with nothing, so there is nothing to review it against. "
		  "Pick a line with Compare with\xE2\x80\xA6 or use Review against.");
}

TEST_CASE("Git window: without a line read a branch has no review base unless it has a session")
{
	const Repository repository;
	ReviewSessions sessions;

	CHECK_FALSE(Graphite::ReviewBaseOf(kSide, repository.Branches(), sessions, nullptr).has_value());

	sessions.Start(kSide, kNext);
	CHECK(Graphite::ReviewBaseOf(kSide, repository.Branches(), sessions, nullptr) == kNext);
}

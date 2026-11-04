#include <doctest/doctest.h>

#include "Git/History/CommitDetails.h"
#include "Git/Lines/LineReadFixtures.h"

#include "Core/Git/Commit.h"
#include "Core/Git/Diff.h"
#include "Core/Git/Signature.h"

#include <chrono>
#include <optional>
#include <vector>

namespace {
	std::chrono::sys_seconds At(long long seconds)
	{
		return std::chrono::sys_seconds{std::chrono::seconds{seconds}};
	}

	Core::Hunk HunkOf(std::initializer_list<Core::DiffLineKind> kinds)
	{
		Core::Hunk hunk;

		for (const Core::DiffLineKind kind : kinds)
		{
			hunk.Lines.push_back(Core::DiffLine{.Kind = kind});
		}

		return hunk;
	}

	Core::Signature Who(std::string name, std::string email, long long when)
	{
		return Core::Signature{.Name = std::move(name), .Email = std::move(email), .When = At(when)};
	}
}

TEST_CASE("A diff with no files counts nothing")
{
	const Graphite::DiffStats stats = Graphite::CountChanges(Core::Diff{});

	CHECK(stats.Files == 0);
	CHECK(stats.Added == 0);
	CHECK(stats.Removed == 0);
}

TEST_CASE("Added and removed lines are counted across every hunk of every file")
{
	Core::Diff diff;

	Core::FileDiff first;
	first.Hunks.push_back(HunkOf({Core::DiffLineKind::Added, Core::DiffLineKind::Added}));
	first.Hunks.push_back(HunkOf({Core::DiffLineKind::Removed}));

	Core::FileDiff second;
	second.Hunks.push_back(HunkOf({Core::DiffLineKind::Added, Core::DiffLineKind::Removed}));

	diff.Files.push_back(first);
	diff.Files.push_back(second);

	const Graphite::DiffStats stats = Graphite::CountChanges(diff);

	CHECK(stats.Files == 2);
	CHECK(stats.Added == 3);
	CHECK(stats.Removed == 2);
}

TEST_CASE("Context lines are not counted as changes")
{
	Core::Diff diff;

	Core::FileDiff file;
	file.Hunks.push_back(HunkOf({Core::DiffLineKind::Context, Core::DiffLineKind::Added,
								 Core::DiffLineKind::Context}));

	diff.Files.push_back(file);

	const Graphite::DiffStats stats = Graphite::CountChanges(diff);

	CHECK(stats.Added == 1);
	CHECK(stats.Removed == 0);
}

TEST_CASE("A binary file counts as a changed file with no lines")
{
	Core::Diff diff;
	diff.Files.push_back(Core::FileDiff{.IsBinary = true});

	const Graphite::DiffStats stats = Graphite::CountChanges(diff);

	CHECK(stats.Files == 1);
	CHECK(stats.Added == 0);
	CHECK(stats.Removed == 0);
}

TEST_CASE("A timestamp reads in the offset it is given, to the minute")
{
	CHECK(Graphite::FormattedTimestamp(At(0), std::chrono::minutes{0}) == "1970-01-01 00:00");
	CHECK(Graphite::FormattedTimestamp(At(0), std::chrono::hours{2}) == "1970-01-01 02:00");
}

TEST_CASE("A negative offset can carry a timestamp back over the date")
{
	CHECK(Graphite::FormattedTimestamp(At(0), -std::chrono::hours{1}) == "1969-12-31 23:00");
}

TEST_CASE("A timestamp in a zone reads in the zone's offset at its own moment")
{
	const std::chrono::time_zone* const warsaw = std::chrono::locate_zone("Europe/Warsaw");

	// 16:42 in Warsaw on both days: summer time in July, standard time in January.
	const std::chrono::sys_seconds summer =
		std::chrono::sys_days{std::chrono::year{2026} / 7 / 14} + std::chrono::hours{14} + std::chrono::minutes{42};
	const std::chrono::sys_seconds winter =
		std::chrono::sys_days{std::chrono::year{2026} / 1 / 14} + std::chrono::hours{15} + std::chrono::minutes{42};

	CHECK(Graphite::FormattedTimestamp(summer, warsaw) == "2026-07-14 16:42");
	CHECK(Graphite::FormattedTimestamp(winter, warsaw) == "2026-01-14 16:42");
}

TEST_CASE("A timestamp with no zone reads as UTC")
{
	const std::chrono::time_zone* const noZone = nullptr;

	CHECK(Graphite::FormattedTimestamp(At(0), noZone) == "1970-01-01 00:00");
}

TEST_CASE("Seconds are dropped rather than rounded")
{
	CHECK(Graphite::FormattedTimestamp(At(59), std::chrono::minutes{0}) == "1970-01-01 00:00");
}

TEST_CASE("A commit whose committer is its author says so only once")
{
	Core::CommitSummary commit;
	commit.Author = Who("Alice", "alice@example.com", 1000);
	commit.Committer = commit.Author;

	CHECK_FALSE(Graphite::ShowsCommitter(commit));
}

TEST_CASE("A commit committed by someone else names them")
{
	Core::CommitSummary commit;
	commit.Author = Who("Alice", "alice@example.com", 1000);
	commit.Committer = Who("Bob", "bob@example.com", 2000);

	CHECK(Graphite::ShowsCommitter(commit));
}

TEST_CASE("The same person at a later date is still one person")
{
	Core::CommitSummary commit;
	commit.Author = Who("Alice", "alice@example.com", 1000);
	commit.Committer = Who("Alice", "alice@example.com", 9000);

	CHECK_FALSE(Graphite::ShowsCommitter(commit));
}

TEST_CASE("A committer with the same name on a different address is a different person")
{
	Core::CommitSummary commit;
	commit.Author = Who("Alice", "alice@example.com", 1000);
	commit.Committer = Who("Alice", "alice@work.example.com", 1000);

	CHECK(Graphite::ShowsCommitter(commit));
}

TEST_CASE("A commit message to copy is the summary, then the body after a blank line")
{
	Core::CommitSummary commit;
	commit.Summary = "Fix the parser";
	CHECK(Graphite::CommitMessageText(commit) == "Fix the parser");

	commit.Body = "It read past the end.";
	CHECK(Graphite::CommitMessageText(commit) == "Fix the parser\n\nIt read past the end.");
}

TEST_CASE("The change count says it is counting while the diff is read, and that it could not once the diff failed")
{
	constexpr bool kDiffFailed = true;
	constexpr bool kDiffOnItsWay = false;

	CHECK(Graphite::UncountedChangesText(kDiffOnItsWay) == "counting changes\xE2\x80\xA6");
	CHECK(Graphite::UncountedChangesText(kDiffFailed) == "couldn't count changes");
}
namespace {

	Graphite::LineRead TwoBasesRead()
	{
		Graphite::Fixtures::LineReadBuilder builder;
		builder.AddRemote("origin");

		const std::size_t develop = builder.AddLocal("develop");
		const std::size_t release = builder.AddRemoteBranch("origin", "release");
		const std::size_t main = builder.AddLocal("main");
		builder.MakeBase(develop);
		builder.MakeBase(release);
		builder.MakeBase(main);
		return builder.Build();
	}
}

TEST_CASE("Git window: details name the bases that hold the commit")
{
	const Graphite::LineRead read = TwoBasesRead();

	const Graphite::InBases bases = Graphite::DescribeInBases(read, std::vector<bool>{true, false, true});

	CHECK_FALSE(bases.Pending);
	REQUIRE(bases.Holding.size() == 2);
	CHECK(bases.Holding[0] == "develop");
	CHECK(bases.Holding[1] == "main");

	const Graphite::InBases withRemote = Graphite::DescribeInBases(read, std::vector<bool>{false, true, false});

	REQUIRE(withRemote.Holding.size() == 1);
	CHECK(withRemote.Holding[0] == "origin/release");
}

TEST_CASE("Git window: details say in no base yet")
{
	const Graphite::LineRead read = TwoBasesRead();

	const Graphite::InBases bases = Graphite::DescribeInBases(read, std::vector<bool>{false, false, false});

	CHECK_FALSE(bases.Pending);
	CHECK(bases.Holding.empty());
}

TEST_CASE("Git window: the in bases line waits with an ellipsis")
{
	const Graphite::LineRead read = TwoBasesRead();

	const Graphite::InBases bases = Graphite::DescribeInBases(read, std::nullopt);

	CHECK(bases.Pending);
	CHECK(bases.Holding.empty());
}

TEST_CASE("Git window: the in bases line is unknown once the line read failed and waits while one is coming")
{
	const Core::Error failure{Core::ErrorCode::IoFailure, "graph.bin is unreadable"};

	const Graphite::InBases failed = Graphite::InBasesWithoutRead(false, failure);

	CHECK_FALSE(failed.Pending);
	REQUIRE(failed.Unknown.has_value());
	CHECK(*failed.Unknown == "graph.bin is unreadable");

	CHECK(Graphite::InBasesWithoutRead(true, failure).Pending);
	CHECK(Graphite::InBasesWithoutRead(false, std::nullopt).Pending);
	CHECK_FALSE(Graphite::InBasesWithoutRead(false, std::nullopt).Unknown.has_value());
}

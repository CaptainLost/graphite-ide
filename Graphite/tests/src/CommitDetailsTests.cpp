#include <doctest/doctest.h>

#include "Git/History/CommitDetails.h"

#include "Core/Git/Commit.h"
#include "Core/Git/Diff.h"
#include "Core/Git/Signature.h"

#include <chrono>

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
	// A hunk is mostly context, and counting it would make every summary read as though
	// the commit rewrote the surrounding code.
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
	// A binary delta carries no hunks at all, so the only honest thing to say about it is
	// that the file changed.
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
	// The instant is midnight UTC; an hour west of it, the reader's calendar still says the
	// day before, and a date that ignored the offset would name the wrong day.
	CHECK(Graphite::FormattedTimestamp(At(0), -std::chrono::hours{1}) == "1969-12-31 23:00");
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
	// A rebase or an amend leaves the author untouched and moves the committer date. A
	// second line repeating the same name to say so is noise, not information.
	Core::CommitSummary commit;
	commit.Author = Who("Alice", "alice@example.com", 1000);
	commit.Committer = Who("Alice", "alice@example.com", 9000);

	CHECK_FALSE(Graphite::ShowsCommitter(commit));
}

TEST_CASE("A committer with the same name on a different address is a different person")
{
	// Two addresses is how the same display name reaches git from two accounts, and which
	// account committed is exactly what this line exists to say.
	Core::CommitSummary commit;
	commit.Author = Who("Alice", "alice@example.com", 1000);
	commit.Committer = Who("Alice", "alice@work.example.com", 1000);

	CHECK(Graphite::ShowsCommitter(commit));
}

#include <doctest/doctest.h>

#include "Git/CommitLabels.h"

using namespace std::chrono_literals;

namespace {

	std::chrono::sys_seconds At(long long seconds)
	{
		return std::chrono::sys_seconds{std::chrono::seconds{seconds}};
	}
}

TEST_CASE("An age under a minute reads as just now")
{
	CHECK(Graphite::RelativeAge(At(1000), At(1030)) == "just now");
}

TEST_CASE("Ages read in the largest unit that fits")
{
	CHECK(Graphite::RelativeAge(At(0), At(120)) == "2 minutes ago");
	CHECK(Graphite::RelativeAge(At(0), At(7200)) == "2 hours ago");
	CHECK(Graphite::RelativeAge(At(0), At(172800)) == "2 days ago");
}

TEST_CASE("A single unit is not pluralised")
{
	CHECK(Graphite::RelativeAge(At(0), At(60)) == "1 minute ago");
	CHECK(Graphite::RelativeAge(At(0), At(3600)) == "1 hour ago");
}

TEST_CASE("A commit dated in the future reads as just now rather than negatively")
{
	// Clock skew between machines is ordinary, and "in -3 minutes" is not a thing to show
	// a user.
	CHECK(Graphite::RelativeAge(At(5000), At(1000)) == "just now");
}

TEST_CASE("An age of a couple of months reads in months")
{
	// Comfortably inside the branch rather than on its boundary: two months, not the
	// threshold itself.
	CHECK(Graphite::RelativeAge(At(0), At(5184000)) == "2 months ago");
}

TEST_CASE("An age of a couple of years reads in years")
{
	CHECK(Graphite::RelativeAge(At(0), At(63072000)) == "2 years ago");
}

TEST_CASE("Initials come from the first and last word of a name")
{
	Core::Signature who;
	who.Name = "Ada Lovelace";

	CHECK(Graphite::AuthorInitials(who) == "AL");
}

TEST_CASE("A one-word name gives one initial")
{
	Core::Signature who;
	who.Name = "Ada";

	CHECK(Graphite::AuthorInitials(who) == "A");
}

TEST_CASE("A name with a middle name still gives two initials")
{
	Core::Signature who;
	who.Name = "Ada King Lovelace";

	CHECK(Graphite::AuthorInitials(who) == "AL");
}

TEST_CASE("An empty name falls back to the email")
{
	Core::Signature who;
	who.Email = "ada@example.com";

	// A commit with no author name is malformed but real, and a blank badge tells the
	// reader nothing at all.
	CHECK(Graphite::AuthorInitials(who) == "A");
}

TEST_CASE("A name with neither name nor email gives a placeholder")
{
	CHECK(Graphite::AuthorInitials(Core::Signature{}) == "?");
}

TEST_CASE("A trailing space on the name does not garble the initials")
{
	Core::Signature who;
	who.Name = "Ada Lovelace ";

	CHECK(Graphite::AuthorInitials(who) == "AL");
}

TEST_CASE("A leading space on the name does not garble the initials")
{
	Core::Signature who;
	who.Name = " Ada Lovelace";

	CHECK(Graphite::AuthorInitials(who) == "AL");
}

TEST_CASE("A whitespace-only name falls back to the email like an empty one")
{
	Core::Signature who;
	who.Name = " ";
	who.Email = "ada@example.com";

	CHECK(Graphite::AuthorInitials(who) == "A");
}

TEST_CASE("Every kind of file change has its own letter")
{
	CHECK(Graphite::ChangeLetter(Core::FileChange::Added) == 'A');
	CHECK(Graphite::ChangeLetter(Core::FileChange::Modified) == 'M');
	CHECK(Graphite::ChangeLetter(Core::FileChange::Deleted) == 'D');
	CHECK(Graphite::ChangeLetter(Core::FileChange::Renamed) == 'R');
	CHECK(Graphite::ChangeLetter(Core::FileChange::Copied) == 'C');
	CHECK(Graphite::ChangeLetter(Core::FileChange::TypeChanged) == 'T');

	// Never reaches a diff's file list in practice, but a blank is a safer default than a
	// claim about a change that did not happen.
	CHECK(Graphite::ChangeLetter(Core::FileChange::Unmodified) == ' ');
}

TEST_CASE("A modified file is shown under its new path")
{
	Core::FileDiff file;
	file.OldPath = "a.txt";
	file.NewPath = "a.txt";
	file.Change = Core::FileChange::Modified;

	CHECK(Graphite::DisplayPath(file) == std::filesystem::path("a.txt"));
}

TEST_CASE("A deleted file is shown under its old path")
{
	Core::FileDiff file;
	file.OldPath = "gone.txt";
	file.Change = Core::FileChange::Deleted;

	// A deleted delta carries no new path at all, so falling back to NewPath would show
	// nothing for exactly the change a reader most wants to find by name.
	CHECK(Graphite::DisplayPath(file) == std::filesystem::path("gone.txt"));
}

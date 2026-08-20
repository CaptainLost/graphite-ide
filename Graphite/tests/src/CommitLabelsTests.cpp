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

TEST_CASE("A short name is padded to exactly the requested width")
{
	Core::Signature who;
	who.Name = "Ada";

	const std::string padded = Graphite::PaddedAuthor(who, 8);

	CHECK(padded.size() == 8);
	CHECK(padded == "Ada     ");
}

TEST_CASE("A long name is cut to exactly the requested width and ends in the ellipsis")
{
	Core::Signature who;
	who.Name = "Augusta Ada King, Countess of Lovelace";

	const std::string cut = Graphite::PaddedAuthor(who, 10);

	CHECK(cut.size() == 10);
	// U+2026, the single-character ellipsis, encoded as UTF-8: 7 kept bytes + 3 for it.
	CHECK(cut == "Augusta\xE2\x80\xA6");
}

TEST_CASE("An empty name falls back to the email's local part")
{
	Core::Signature who;
	who.Email = "ada@example.com";

	CHECK(Graphite::PaddedAuthor(who, 6) == "ada   ");
}

TEST_CASE("Neither name nor email gives unknown")
{
	const std::string padded = Graphite::PaddedAuthor(Core::Signature{}, 9);

	CHECK(padded.size() == 9);
	CHECK(padded == "unknown  ");
}

TEST_CASE("A width smaller than the ellipsis itself degrades to a plain, padded cut")
{
	Core::Signature who;
	who.Name = "Augusta Ada King";

	// Too narrow to fit even the single ellipsis character (3 bytes in UTF-8), so this
	// falls back to a plain prefix rather than emitting a truncated, meaningless ellipsis.
	// It still returns exactly `width` bytes.
	const std::string cut = Graphite::PaddedAuthor(who, 2);

	CHECK(cut.size() == 2);
	CHECK(cut == "Au");
}

TEST_CASE("A width of zero returns an empty string rather than underflowing")
{
	Core::Signature who;
	who.Name = "Ada Lovelace";

	CHECK(Graphite::PaddedAuthor(who, 0).empty());
}

TEST_CASE("Cutting a name never splits a multi-byte UTF-8 character")
{
	Core::Signature who;
	// Each of these is a two-byte UTF-8 character (Latin-1 Supplement), so a byte-boundary
	// cut that ignored character boundaries would land inside one and break it.
	who.Name = "Ren\xC3\xA9 D\xC3\xA9j\xC3\xA0 Vu"; // "René Déjà Vu"

	// At width 7 the naive cut point (width - ellipsis size = 4 bytes) lands on 0xA9, the
	// second byte of "é" (0xC3 0xA9). A byte-blind cut would keep "Ren\xC3" — a lead byte
	// with no continuation, i.e. a broken glyph. The safe cut instead backs up to the
	// character boundary before "é" and keeps only "Ren", padding out the byte it gave up.
	const std::string cut = Graphite::PaddedAuthor(who, 7);

	CHECK(cut.size() == 7);
	CHECK(cut == "Ren\xE2\x80\xA6 ");
}

TEST_CASE("PaddedAge cuts a long age string to exactly the requested width")
{
	const std::string cut = Graphite::PaddedAge(At(0), At(63072000), 6);

	CHECK(cut.size() == 6);
	// "2 years ago" cut to 3 kept bytes ("2 y") plus the 3-byte ellipsis.
	CHECK(cut == "2 y\xE2\x80\xA6");
}

TEST_CASE("PaddedAge pads a short age string to exactly the requested width")
{
	const std::string padded = Graphite::PaddedAge(At(1000), At(1030), 12);

	CHECK(padded.size() == 12);
	CHECK(padded == "just now    ");
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

	CHECK(Graphite::ShownPath(file) == std::filesystem::path("a.txt"));
}

TEST_CASE("A deleted file is shown under its old path")
{
	Core::FileDiff file;
	file.OldPath = "gone.txt";
	file.Change = Core::FileChange::Deleted;

	// A deleted delta carries no new path at all, so falling back to NewPath would show
	// nothing for exactly the change a reader most wants to find by name.
	CHECK(Graphite::ShownPath(file) == std::filesystem::path("gone.txt"));
}

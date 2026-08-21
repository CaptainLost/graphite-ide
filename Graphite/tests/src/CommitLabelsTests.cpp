#include <doctest/doctest.h>

#include "Git/CommitLabels.h"
#include "Git/GitPalette.h"

#include <cstddef>
#include <string>
#include <string_view>

using namespace std::chrono_literals;

namespace {

	std::chrono::sys_seconds At(long long seconds)
	{
		return std::chrono::sys_seconds{std::chrono::seconds{seconds}};
	}

	// Mirrors the production code-point counting in CommitLabels.cpp: a UTF-8 continuation
	// byte has the top two bits `10`, so counting the bytes that are not one counts code
	// points without decoding anything. PaddedAuthor/PaddedAge promise `width` in this
	// unit, not in bytes, so this is what the tests below must assert against.
	std::size_t CodePointCount(std::string_view text)
	{
		std::size_t count = 0;

		for (const char byte : text)
		{
			if ((static_cast<unsigned char>(byte) & 0xC0) != 0x80)
			{
				++count;
			}
		}

		return count;
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

	CHECK(CodePointCount(padded) == 8);
	CHECK(padded == "Ada     ");
}

TEST_CASE("A long name is cut to exactly the requested width and ends in the ellipsis")
{
	Core::Signature who;
	who.Name = "Augusta Ada King, Countess of Lovelace";

	const std::string cut = Graphite::PaddedAuthor(who, 10);

	CHECK(CodePointCount(cut) == 10);
	// U+2026, the single-character ellipsis, encoded as UTF-8: 9 kept code points
	// ("Augusta A") + the ellipsis as the 10th.
	CHECK(cut == "Augusta A\xE2\x80\xA6");
}

TEST_CASE("An ASCII name long enough to be cut occupies exactly width columns, not fewer")
{
	// This is the case a byte-based contract got wrong: reserving 3 *bytes* for the
	// (1-column) ellipsis threw away two columns of budget on any ASCII name long enough
	// to be cut, so a cut row's summary started two characters left of a short row's.
	Core::Signature who;
	who.Name = "Alexandria Ocasio-Cortez";

	const std::string cut = Graphite::PaddedAuthor(who, 12);

	CHECK(CodePointCount(cut) == 12);
	// 11 kept code points ("Alexandria ") + the ellipsis as the 12th — 14 bytes, since the
	// ellipsis alone is 3 bytes for 1 column; byte count is deliberately not asserted here.
	CHECK(cut == "Alexandria \xE2\x80\xA6");
}

TEST_CASE("A padded multi-byte name occupies exactly width columns")
{
	// The other case a byte-based contract got wrong: padding a name with an accented
	// character to a byte count left it one column short per multi-byte character, since
	// each accented letter here is 2 bytes but only 1 column.
	Core::Signature who;
	who.Name = "Ren\xC3\xA9"; // "René": 4 code points, 5 bytes.

	const std::string padded = Graphite::PaddedAuthor(who, 8);

	CHECK(CodePointCount(padded) == 8);
	CHECK(padded == "Ren\xC3\xA9    "); // 5 bytes of "René" + 4 spaces = 8 columns.
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

	CHECK(CodePointCount(padded) == 9);
	CHECK(padded == "unknown  ");
}

TEST_CASE("A width of one is too small for anything but the ellipsis itself")
{
	Core::Signature who;
	who.Name = "Augusta Ada King";

	// One column is exactly the ellipsis's own width, so the cut prefix is empty rather
	// than underflowing or omitting the ellipsis. Still exactly `width` columns.
	const std::string cut = Graphite::PaddedAuthor(who, 1);

	CHECK(CodePointCount(cut) == 1);
	CHECK(cut == "\xE2\x80\xA6");
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
	// Each of these is a two-byte UTF-8 character (Latin-1 Supplement). Cutting by code
	// point rather than by byte offset makes splitting one structurally impossible: the
	// cut always keeps or drops a whole lead-byte-plus-continuation-bytes group.
	who.Name = "Ren\xC3\xA9 D\xC3\xA9j\xC3\xA0 Vu"; // "René Déjà Vu"

	// Width 5 keeps 4 code points — "René" ("Ren" plus the whole 2-byte "é") — then adds
	// the ellipsis as the 5th. A byte-blind cut at the equivalent byte offset would have
	// kept "Ren\xC3" instead: a lead byte with no continuation, i.e. a broken glyph.
	const std::string cut = Graphite::PaddedAuthor(who, 5);

	CHECK(CodePointCount(cut) == 5);
	CHECK(cut == "Ren\xC3\xA9\xE2\x80\xA6");
}

TEST_CASE("PaddedAge cuts a long age string to exactly the requested width")
{
	const std::string cut = Graphite::PaddedAge(At(0), At(63072000), 6);

	CHECK(CodePointCount(cut) == 6);
	// "2 years ago" cut to 5 kept code points ("2 yea") plus the ellipsis as the 6th.
	CHECK(cut == "2 yea\xE2\x80\xA6");
}

TEST_CASE("PaddedAge pads a short age string to exactly the requested width")
{
	const std::string padded = Graphite::PaddedAge(At(1000), At(1030), 12);

	CHECK(CodePointCount(padded) == 12);
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

TEST_CASE("A change letter takes the colour that change has everywhere else")
{
	// The gutter colours, not new ones: a file added in a commit and a line added in the
	// editor are the same event to a reader, and two greens would say they are not.
	Graphite::GitPalette palette;
	palette.GutterAdded = 0x11111111;
	palette.GutterModified = 0x22222222;
	palette.GutterRemoved = 0x33333333;

	CHECK(Graphite::ChangeColor(Core::FileChange::Added, palette) == palette.GutterAdded);
	CHECK(Graphite::ChangeColor(Core::FileChange::Modified, palette) == palette.GutterModified);
	CHECK(Graphite::ChangeColor(Core::FileChange::Deleted, palette) == palette.GutterRemoved);
}

TEST_CASE("A change that neither adds nor deletes the file reads as a modification")
{
	// A rename, a copy and a type change all leave a file where it was with something about
	// it different, which is what the modified colour says.
	Graphite::GitPalette palette;
	palette.GutterModified = 0x22222222;

	CHECK(Graphite::ChangeColor(Core::FileChange::Renamed, palette) == palette.GutterModified);
	CHECK(Graphite::ChangeColor(Core::FileChange::Copied, palette) == palette.GutterModified);
	CHECK(Graphite::ChangeColor(Core::FileChange::TypeChanged, palette) == palette.GutterModified);
}

TEST_CASE("A file the commit left alone is not coloured at all")
{
	// Its letter is a space. Colouring nothing would still tint the row's spacing, which is
	// a colour with no meaning attached to it.
	Graphite::GitPalette palette;
	palette.GutterAdded = 0x11111111;
	palette.GutterModified = 0x22222222;
	palette.GutterRemoved = 0x33333333;

	CHECK(Graphite::ChangeColor(Core::FileChange::Unmodified, palette) == 0u);
}

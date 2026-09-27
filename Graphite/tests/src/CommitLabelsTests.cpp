#include <doctest/doctest.h>

#include "Git/CommitLabels.h"
#include "Git/GitPalette.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

using namespace std::chrono_literals;

namespace {

	std::chrono::sys_seconds At(long long seconds)
	{
		return std::chrono::sys_seconds{std::chrono::seconds{seconds}};
	}

	// A UTF-8 continuation byte has the top two bits `10`, so counting the bytes that are
	// not one counts code points without decoding anything. The cut tests below measure text in this unit,
	// standing in for a monospace font's columns.
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

TEST_CASE("The author column shows the name as git recorded it")
{
	Core::Signature who;
	who.Name = "Ada Lovelace";
	who.Email = "ada@example.com";

	CHECK(Graphite::AuthorName(who) == "Ada Lovelace");
}

TEST_CASE("An empty name falls back to the email's local part")
{
	Core::Signature who;
	who.Email = "ada@example.com";

	CHECK(Graphite::AuthorName(who) == "ada");
}

TEST_CASE("Neither name nor email gives unknown")
{
	CHECK(Graphite::AuthorName(Core::Signature{}) == "unknown");
}

namespace {

	// A monospace face one unit per code point, which is what the list is drawn in and
	// what makes the expected cuts below countable by hand.
	float Columns(std::string_view text)
	{
		return static_cast<float>(CodePointCount(text));
	}
}

TEST_CASE("Text that fits is not cut")
{
	CHECK_FALSE(Graphite::CutToFit("fix: a bug", 10.0f, 1.0f, Columns).has_value());
	CHECK_FALSE(Graphite::CutToFit("", 0.0f, 1.0f, Columns).has_value());
}

TEST_CASE("Text too long keeps as much as fits beside the ellipsis")
{
	// Ten columns, one of them the ellipsis's: nine kept.
	const std::optional<std::size_t> cut =
		Graphite::CutToFit("abcdefghijklmnop", 10.0f, 1.0f, Columns);

	REQUIRE(cut.has_value());
	CHECK(*cut == 9);
}

TEST_CASE("A cut drops the spaces it would otherwise end on")
{
	// "fix: end the" is twelve columns and would leave "fix: end the …" with the ellipsis
	// hanging a space away from the word it cuts short.
	const std::optional<std::size_t> cut =
		Graphite::CutToFit("fix: end the changes", 14.0f, 1.0f, Columns);

	REQUIRE(cut.has_value());
	CHECK(std::string_view("fix: end the changes").substr(0, *cut) == "fix: end the");
}

TEST_CASE("A cut never splits a multi-byte UTF-8 character")
{
	// "René Déjà Vu": the fourth code point is a two-byte "é", and a byte-counted cut at
	// the same point would keep only its lead byte, which draws as a broken glyph.
	const std::string_view name = "Ren\xC3\xA9 D\xC3\xA9j\xC3\xA0 Vu";

	const std::optional<std::size_t> cut = Graphite::CutToFit(name, 5.0f, 1.0f, Columns);

	REQUIRE(cut.has_value());
	CHECK(name.substr(0, *cut) == "Ren\xC3\xA9");
}

TEST_CASE("Text that starts mid-character is cut by width all the same")
{
	// Three Latin-1 bytes a UTF-8 reader sees as lone continuation bytes, measured the way a
	// font draws them: one replacement glyph each. None of them starts a code point, and the
	// cut must still not keep them unmeasured and overflow the cell.
	const std::string_view text = "\xB0\xB0\xB0"
								  "abc";

	const auto bytes = [](std::string_view piece) { return static_cast<float>(piece.size()); };

	const std::optional<std::size_t> cut = Graphite::CutToFit(text, 3.0f, 1.0f, bytes);

	REQUIRE(cut.has_value());
	CHECK(*cut == 0);
}

TEST_CASE("A cell narrower than the ellipsis keeps nothing rather than underflowing")
{
	const std::optional<std::size_t> cut = Graphite::CutToFit("Ada Lovelace", 0.5f, 1.0f, Columns);

	REQUIRE(cut.has_value());
	CHECK(*cut == 0);
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

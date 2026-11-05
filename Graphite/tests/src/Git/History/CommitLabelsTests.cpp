#include <doctest/doctest.h>

#include "Git/History/CommitLabels.h"
#include "Theme/GitPalette.h"


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
	CHECK(Graphite::RelativeAge(At(5000), At(1000)) == "just now");
}

TEST_CASE("An age of a couple of months reads in months")
{
	CHECK(Graphite::RelativeAge(At(0), At(5184000)) == "2 months ago");
}

TEST_CASE("An age of a couple of years reads in years")
{
	CHECK(Graphite::RelativeAge(At(0), At(63072000)) == "2 years ago");
}

TEST_CASE("Initials come from the first and last word of a name")
{
	Core::Signature signature;
	signature.Name = "Ada Lovelace";

	CHECK(Graphite::AuthorInitials(signature) == "AL");
}

TEST_CASE("A one-word name gives one initial")
{
	Core::Signature signature;
	signature.Name = "Ada";

	CHECK(Graphite::AuthorInitials(signature) == "A");
}

TEST_CASE("A name with a middle name still gives two initials")
{
	Core::Signature signature;
	signature.Name = "Ada King Lovelace";

	CHECK(Graphite::AuthorInitials(signature) == "AL");
}

TEST_CASE("An empty name falls back to the email")
{
	Core::Signature signature;
	signature.Email = "ada@example.com";

	CHECK(Graphite::AuthorInitials(signature) == "A");
}

TEST_CASE("A name with neither name nor email gives a placeholder")
{
	CHECK(Graphite::AuthorInitials(Core::Signature{}) == "?");
}

TEST_CASE("A trailing space on the name does not garble the initials")
{
	Core::Signature signature;
	signature.Name = "Ada Lovelace ";

	CHECK(Graphite::AuthorInitials(signature) == "AL");
}

TEST_CASE("A leading space on the name does not garble the initials")
{
	Core::Signature signature;
	signature.Name = " Ada Lovelace";

	CHECK(Graphite::AuthorInitials(signature) == "AL");
}

TEST_CASE("A whitespace-only name falls back to the email like an empty one")
{
	Core::Signature signature;
	signature.Name = " ";
	signature.Email = "ada@example.com";

	CHECK(Graphite::AuthorInitials(signature) == "A");
}

TEST_CASE("The author column shows the name as git recorded it")
{
	Core::Signature signature;
	signature.Name = "Ada Lovelace";
	signature.Email = "ada@example.com";

	CHECK(Graphite::AuthorName(signature) == "Ada Lovelace");
}

TEST_CASE("An empty name falls back to the email's local part")
{
	Core::Signature signature;
	signature.Email = "ada@example.com";

	CHECK(Graphite::AuthorName(signature) == "ada");
}

TEST_CASE("Neither name nor email gives unknown")
{
	CHECK(Graphite::AuthorName(Core::Signature{}) == "unknown");
}

TEST_CASE("Every kind of file change has its own letter")
{
	CHECK(Graphite::ChangeLetter(Core::FileChange::Added) == 'A');
	CHECK(Graphite::ChangeLetter(Core::FileChange::Modified) == 'M');
	CHECK(Graphite::ChangeLetter(Core::FileChange::Deleted) == 'D');
	CHECK(Graphite::ChangeLetter(Core::FileChange::Renamed) == 'R');
	CHECK(Graphite::ChangeLetter(Core::FileChange::Copied) == 'C');
	CHECK(Graphite::ChangeLetter(Core::FileChange::TypeChanged) == 'T');

	CHECK(Graphite::ChangeLetter(Core::FileChange::Unmodified) == ' ');
}

TEST_CASE("Every letter has a name for its tooltip")
{
	CHECK(std::string{Graphite::ChangeName(Core::FileChange::Added)} == "Added");
	CHECK(std::string{Graphite::ChangeName(Core::FileChange::Modified)} == "Modified");
	CHECK(std::string{Graphite::ChangeName(Core::FileChange::Deleted)} == "Deleted");
	CHECK(std::string{Graphite::ChangeName(Core::FileChange::Renamed)} == "Renamed");
	CHECK(std::string{Graphite::ChangeName(Core::FileChange::Copied)} == "Copied");
	CHECK(std::string{Graphite::ChangeName(Core::FileChange::TypeChanged)} == "Type changed");
	CHECK(Graphite::ChangeName(Core::FileChange::Unmodified) == nullptr);
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

	CHECK(Graphite::ShownPath(file) == std::filesystem::path("gone.txt"));
}

TEST_CASE("A change letter takes the colour that change has everywhere else")
{
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
	Graphite::GitPalette palette;
	palette.GutterModified = 0x22222222;

	CHECK(Graphite::ChangeColor(Core::FileChange::Renamed, palette) == palette.GutterModified);
	CHECK(Graphite::ChangeColor(Core::FileChange::Copied, palette) == palette.GutterModified);
	CHECK(Graphite::ChangeColor(Core::FileChange::TypeChanged, palette) == palette.GutterModified);
}

TEST_CASE("A file the commit left alone is not coloured at all")
{
	Graphite::GitPalette palette;
	palette.GutterAdded = 0x11111111;
	palette.GutterModified = 0x22222222;
	palette.GutterRemoved = 0x33333333;

	CHECK(Graphite::ChangeColor(Core::FileChange::Unmodified, palette) == 0u);
}

TEST_CASE("A file is found by its new path")
{
	Core::Diff diff;
	diff.Files.push_back(Core::FileDiff{.OldPath = "a.txt", .NewPath = "a.txt"});
	diff.Files.push_back(Core::FileDiff{.OldPath = "b.txt", .NewPath = "c.txt"});

	CHECK(Graphite::IndexOfFile(diff, "c.txt") == 1);
}

TEST_CASE("A deleted file with no new path is found by its old path")
{
	Core::Diff diff;
	diff.Files.push_back(Core::FileDiff{.OldPath = "a.txt", .NewPath = "a.txt"});
	diff.Files.push_back(Core::FileDiff{.OldPath = "gone.txt"});

	CHECK(Graphite::IndexOfFile(diff, "gone.txt") == 1);
}

TEST_CASE("A path no file in the diff carries is not found")
{
	Core::Diff diff;
	diff.Files.push_back(Core::FileDiff{.OldPath = "a.txt", .NewPath = "a.txt"});

	CHECK_FALSE(Graphite::IndexOfFile(diff, "missing.txt").has_value());
}

TEST_CASE("Git window: a short age reads in the largest unit that fits, as the mockup's")
{
	const auto shortAge = [](long long seconds) { return Graphite::ShortAge(At(0), At(seconds)); };

	CHECK(shortAge(0) == "1m");
	CHECK(shortAge(59 * 60) == "59m");
	CHECK(shortAge(60 * 60) == "1h");
	CHECK(shortAge(23 * 3600) == "23h");
	CHECK(shortAge(13 * 86400) == "13d");
	CHECK(shortAge(14 * 86400) == "2w");
	CHECK(shortAge(59 * 86400) == "8w");
	CHECK(shortAge(60 * 86400) == "2mo");
	CHECK(shortAge(364 * 86400) == "12mo");
	CHECK(shortAge(365 * 86400) == "1y");
}

TEST_CASE("Git window: a commit dated in the future has a short age of one minute")
{
	CHECK(Graphite::ShortAge(At(5000), At(1000)) == "1m");
}

TEST_CASE("Git window: an author's avatar hue is stable for one email and does not depend on its case")
{
	CHECK(Graphite::AvatarHue("ada@example.com") == Graphite::AvatarHue("ada@example.com"));
	CHECK(Graphite::AvatarHue("ada@example.com") == Graphite::AvatarHue("Ada@Example.com"));
	CHECK(Graphite::AvatarHue("ada@example.com") != Graphite::AvatarHue("grace@example.com"));
	CHECK(Graphite::AvatarHue("ada@example.com") >= 0);
	CHECK(Graphite::AvatarHue("ada@example.com") < 360);
}

TEST_CASE("Git window: an avatar is filled with the mockup's saturation 34 percent and lightness 36 percent")
{
	CHECK(Graphite::AvatarFill(0) == IM_COL32(123, 61, 61, 255));
	CHECK(Graphite::AvatarFill(120) == IM_COL32(61, 123, 61, 255));
	CHECK(Graphite::AvatarFill(240) == IM_COL32(61, 61, 123, 255));
}

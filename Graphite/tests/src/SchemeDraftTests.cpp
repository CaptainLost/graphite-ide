#include <doctest/doctest.h>

#include "TempDirectory.h"
#include "Theme/BuiltInSchemes.h"
#include "Theme/SchemeDraft.h"
#include "Theme/SchemeStore.h"

#include <algorithm>
#include <filesystem>
#include <fstream>

namespace {

	void WriteFile(const std::filesystem::path& path, std::string_view contents)
	{
		std::ofstream out(path, std::ios::binary);
		out << contents;
	}

	constexpr std::size_t kComment = static_cast<std::size_t>(Graphite::TokenKind::Comment);
	constexpr std::size_t kKeyword = static_cast<std::size_t>(Graphite::TokenKind::Keyword);

	// Channels chosen as exact multiples of 1/255 so they survive the scheme file's eight-bit
	// hex round-trip: 0.75 would come back as 191/255, and a test that cannot round-trip its own
	// fixture fails for a reason that has nothing to do with what it is testing.
	constexpr Core::Color kTestColor{0.2f, 0.4f, 0.8f, 1.0f};
}

TEST_CASE("A fresh draft lists both built-ins and starts read-only")
{
	const TempDirectory temp;

	const Graphite::SchemeDraft draft(temp.GetPath());

	CHECK(std::find(draft.GetNames().begin(), draft.GetNames().end(), "dark") !=
		  draft.GetNames().end());
	CHECK(std::find(draft.GetNames().begin(), draft.GetNames().end(), "light") !=
		  draft.GetNames().end());
	CHECK(draft.IsReadOnly());
}

// A graphite.json that has never named a scheme yields an empty string, which is the state every
// first run is in. Without this rule the draft would look for a file literally called ".json".
TEST_CASE("An empty name selects the dark built-in")
{
	const TempDirectory temp;

	Graphite::SchemeDraft draft(temp.GetPath());

	draft.Select("");

	CHECK(draft.GetSelected() == "dark");
	CHECK(draft.IsReadOnly());
}

TEST_CASE("A built-in refuses edits")
{
	const TempDirectory temp;

	Graphite::SchemeDraft draft(temp.GetPath());

	draft.Select("dark");
	draft.SetColor(Graphite::TokenKind::Comment, kTestColor);

	CHECK_FALSE(draft.IsOverridden(Graphite::TokenKind::Comment));
	CHECK(draft.GetPreview().Syntax[kComment].R ==
		  doctest::Approx(Graphite::DarkScheme().Syntax[kComment].R));
	CHECK_FALSE(draft.Save());
}

// The property the whole sub-project exists to protect: one edit produces one override, not a
// full copy of every colour on screen.
TEST_CASE("Editing one colour overrides exactly one colour")
{
	const TempDirectory temp;

	Graphite::SchemeDraft draft(temp.GetPath());

	draft.Select("dark");
	REQUIRE(draft.Duplicate());
	REQUIRE_FALSE(draft.IsReadOnly());

	draft.SetColor(Graphite::TokenKind::Comment, kTestColor);

	CHECK(draft.IsOverridden(Graphite::TokenKind::Comment));
	CHECK_FALSE(draft.IsOverridden(Graphite::TokenKind::Keyword));
	CHECK(draft.GetPreview().Syntax[kComment].B == doctest::Approx(0.8f));

	REQUIRE(draft.Save());

	// Read it back through the store rather than trusting the draft's own memory.
	const Graphite::ColorScheme saved =
		Graphite::LoadScheme(temp.GetPath() / (draft.GetSelected() + ".json"));

	CHECK(saved.Syntax[kComment].has_value());
	CHECK_FALSE(saved.Syntax[kKeyword].has_value());
}

// Reverting must clear the override, not assign the base colour. Both leave the same pixels on
// screen this version; only one of them still tracks the base in the next one.
TEST_CASE("Reverting clears the override rather than pinning the base value")
{
	const TempDirectory temp;

	Graphite::SchemeDraft draft(temp.GetPath());

	draft.Select("dark");
	REQUIRE(draft.Duplicate());

	draft.SetColor(Graphite::TokenKind::Comment, kTestColor);
	REQUIRE(draft.IsOverridden(Graphite::TokenKind::Comment));

	draft.Revert(Graphite::TokenKind::Comment);

	CHECK_FALSE(draft.IsOverridden(Graphite::TokenKind::Comment));
	CHECK(draft.GetPreview().Syntax[kComment].R ==
		  doctest::Approx(Graphite::DarkScheme().Syntax[kComment].R));

	REQUIRE(draft.Save());

	const Graphite::ColorScheme saved =
		Graphite::LoadScheme(temp.GetPath() / (draft.GetSelected() + ".json"));

	CHECK_FALSE(saved.Syntax[kComment].has_value());
}

TEST_CASE("A duplicate never lands on a name already taken")
{
	const TempDirectory temp;

	Graphite::SchemeDraft draft(temp.GetPath());

	draft.Select("dark");
	REQUIRE(draft.Duplicate());

	const std::string first = draft.GetSelected();

	draft.Select("dark");
	REQUIRE(draft.Duplicate());

	CHECK(draft.GetSelected() != first);
	CHECK(Graphite::UniqueSchemeName(temp.GetPath(), "dark") != first);
	CHECK(Graphite::UniqueSchemeName(temp.GetPath(), "dark") != draft.GetSelected());
}

TEST_CASE("A duplicate carries the source scheme's overrides")
{
	const TempDirectory temp;

	Graphite::ColorScheme source;
	source.Base = Graphite::BuiltInScheme::Light;
	source.Syntax[kComment] = kTestColor;

	REQUIRE(Graphite::SaveScheme(temp.GetPath(), "source", source));

	Graphite::SchemeDraft draft(temp.GetPath());

	draft.Select("source");
	REQUIRE(draft.Duplicate());

	CHECK(draft.IsOverridden(Graphite::TokenKind::Comment));
	CHECK(draft.GetPreview().Syntax[kComment].B == doctest::Approx(0.8f));

	// The base travelled too, or the copy would look nothing like what was copied.
	const std::size_t background = static_cast<std::size_t>(Graphite::EditorColor::Background);

	CHECK(draft.GetPreview().Editor[background].R ==
		  doctest::Approx(Graphite::LightScheme().Editor[background].R));
}

TEST_CASE("Deleting the selected scheme falls back to its base")
{
	const TempDirectory temp;

	Graphite::ColorScheme source;
	source.Base = Graphite::BuiltInScheme::Light;

	REQUIRE(Graphite::SaveScheme(temp.GetPath(), "doomed", source));

	Graphite::SchemeDraft draft(temp.GetPath());

	draft.Select("doomed");
	REQUIRE(draft.DeleteSelected());

	CHECK(draft.GetSelected() == "light");
	CHECK(draft.IsReadOnly());
	CHECK(std::find(draft.GetNames().begin(), draft.GetNames().end(), "doomed") ==
		  draft.GetNames().end());
}

// The file is written by hand because SaveScheme refuses a built-in's name. Without it on disk
// the deletion would fail for want of a file and the test would pass with the refusal removed.
TEST_CASE("Deleting a built-in is refused")
{
	const TempDirectory temp;

	const std::filesystem::path shadow = temp.GetPath() / "dark.json";

	WriteFile(shadow, "{\"base\":\"dark\"}");
	REQUIRE(std::filesystem::exists(shadow));

	Graphite::SchemeDraft draft(temp.GetPath());

	draft.Select("dark");

	CHECK_FALSE(draft.DeleteSelected());
	CHECK(draft.GetSelected() == "dark");
	CHECK(std::filesystem::exists(shadow));
}

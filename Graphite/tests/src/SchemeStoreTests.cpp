#include <doctest/doctest.h>

#include "TempDirectory.h"
#include "Theme/ActiveScheme.h"
#include "Theme/BuiltInSchemes.h"
#include "Theme/SchemeStore.h"

#include <algorithm>
#include <fstream>

namespace {

	void WriteFile(const std::filesystem::path& path, std::string_view contents)
	{
		std::ofstream out(path, std::ios::binary);
		out << contents;
	}
}

TEST_CASE("A saved scheme comes back with the overrides it had")
{
	const TempDirectory temp;

	Graphite::ColorScheme scheme;
	scheme.Base = Graphite::BuiltInScheme::Dark;
	scheme.Syntax[static_cast<std::size_t>(Graphite::TokenKind::Keyword)] =
		Core::Color{1.0f, 0.0f, 0.0f, 1.0f};

	REQUIRE(Graphite::SaveScheme(temp.GetPath(), "mine", scheme));

	const Graphite::ColorScheme loaded =
		Graphite::LoadScheme(temp.GetPath() / "mine.json");

	CHECK(loaded.Base == Graphite::BuiltInScheme::Dark);
	REQUIRE(loaded.Syntax[static_cast<std::size_t>(Graphite::TokenKind::Keyword)].has_value());
	CHECK(loaded.Syntax[static_cast<std::size_t>(Graphite::TokenKind::Keyword)]->R ==
		  doctest::Approx(1.0f));
}

// A scheme that stops being sparse stops surviving a new token kind, so this is the round trip
// that matters: what came from the base must not be written back as an override.
TEST_CASE("Saving does not write colours the scheme never overrode")
{
	const TempDirectory temp;

	Graphite::ColorScheme scheme;
	scheme.Base = Graphite::BuiltInScheme::Dark;
	scheme.Syntax[static_cast<std::size_t>(Graphite::TokenKind::Comment)] =
		Core::Color{0.0f, 1.0f, 0.0f, 1.0f};

	REQUIRE(Graphite::SaveScheme(temp.GetPath(), "sparse", scheme));

	const Graphite::ColorScheme loaded =
		Graphite::LoadScheme(temp.GetPath() / "sparse.json");

	CHECK_FALSE(loaded.Syntax[static_cast<std::size_t>(Graphite::TokenKind::Keyword)].has_value());
	CHECK_FALSE(
		loaded.Editor[static_cast<std::size_t>(Graphite::EditorColor::Selection)].has_value());
}

TEST_CASE("A colour that will not parse is skipped and the rest of the file survives")
{
	const TempDirectory temp;

	WriteFile(temp.GetPath() / "broken.json",
			  R"({"base":"dark","syntax.character":"nonsense","syntax.comment":"6A9955"})");

	const Graphite::ColorScheme loaded =
		Graphite::LoadScheme(temp.GetPath() / "broken.json");

	CHECK(loaded.Base == Graphite::BuiltInScheme::Dark);
	CHECK_FALSE(
		loaded.Syntax[static_cast<std::size_t>(Graphite::TokenKind::Character)].has_value());
	CHECK(loaded.Syntax[static_cast<std::size_t>(Graphite::TokenKind::Comment)].has_value());
}

TEST_CASE("An unrecognised key is skipped rather than refusing the file")
{
	const TempDirectory temp;

	WriteFile(temp.GetPath() / "future.json",
			  R"({"base":"dark","syntax.aardvark":"C586C0","syntax.comment":"6A9955"})");

	const Graphite::ColorScheme loaded =
		Graphite::LoadScheme(temp.GetPath() / "future.json");

	CHECK(loaded.Syntax[static_cast<std::size_t>(Graphite::TokenKind::Comment)].has_value());
}

// A base nobody has heard of falls back to dark, and the scheme's own overrides still apply on
// top of it. Before this task that decision lived at resolution time; it belongs here, where the
// text that caused it was read.
TEST_CASE("A scheme naming an unknown base falls back to dark and keeps its overrides")
{
	const TempDirectory temp;

	WriteFile(temp.GetPath() / "odd.json",
			  R"({"base":"solarized","syntax.comment":"6A9955"})");

	const Graphite::ColorScheme loaded = Graphite::LoadScheme(temp.GetPath() / "odd.json");

	CHECK(loaded.Base == Graphite::BuiltInScheme::Dark);
	CHECK(loaded.Syntax[static_cast<std::size_t>(Graphite::TokenKind::Comment)].has_value());
}

TEST_CASE("A file that is not there loads as an empty scheme rather than failing")
{
	const TempDirectory temp;

	const Graphite::ColorScheme loaded =
		Graphite::LoadScheme(temp.GetPath() / "absent.json");

	CHECK(loaded.Base == Graphite::BuiltInScheme::Dark);
	CHECK_FALSE(loaded.Syntax[static_cast<std::size_t>(Graphite::TokenKind::Keyword)].has_value());
}

TEST_CASE("A built-in name cannot be written over")
{
	const TempDirectory temp;

	const Graphite::ColorScheme scheme;

	CHECK_FALSE(Graphite::SaveScheme(temp.GetPath(), "dark", scheme));
	CHECK_FALSE(Graphite::SaveScheme(temp.GetPath(), "light", scheme));
	CHECK(Graphite::SaveScheme(temp.GetPath(), "mine", scheme));
}

TEST_CASE("Listing a directory names the schemes in it and ignores the built-ins")
{
	const TempDirectory temp;

	const Graphite::ColorScheme scheme;
	REQUIRE(Graphite::SaveScheme(temp.GetPath(), "one", scheme));
	REQUIRE(Graphite::SaveScheme(temp.GetPath(), "two", scheme));
	WriteFile(temp.GetPath() / "dark.json", R"({"base":"dark"})");
	WriteFile(temp.GetPath() / "notes.txt", "ignored");

	const std::vector<std::string> names = Graphite::ListSchemes(temp.GetPath());

	CHECK(names.size() == 2);
	CHECK(std::find(names.begin(), names.end(), "one") != names.end());
	CHECK(std::find(names.begin(), names.end(), "two") != names.end());
	CHECK(std::find(names.begin(), names.end(), "dark") == names.end());
}

TEST_CASE("A deleted scheme stops being listed")
{
	const TempDirectory temp;

	const Graphite::ColorScheme scheme;
	REQUIRE(Graphite::SaveScheme(temp.GetPath(), "gone", scheme));
	REQUIRE(Graphite::DeleteScheme(temp.GetPath(), "gone"));

	CHECK(Graphite::ListSchemes(temp.GetPath()).empty());
}

TEST_CASE("An empty name resolves to the dark built-in without reading anything")
{
	const TempDirectory temp;

	const Graphite::ResolvedScheme resolved = Graphite::ResolveNamed("", temp.GetPath());

	const std::size_t background = static_cast<std::size_t>(Graphite::EditorColor::Background);

	CHECK(resolved.Editor[background].R ==
		  doctest::Approx(Graphite::DarkScheme().Editor[background].R));
}

TEST_CASE("A scheme naming light as its base inherits from light")
{
	const TempDirectory temp;

	WriteFile(temp.GetPath() / "pale.json", R"({"base":"light"})");

	const Graphite::ResolvedScheme resolved = Graphite::ResolveNamed("pale", temp.GetPath());

	const std::size_t background = static_cast<std::size_t>(Graphite::EditorColor::Background);

	CHECK(resolved.Editor[background].R ==
		  doctest::Approx(Graphite::LightScheme().Editor[background].R));
}

// A bare built-in name must short-circuit before the file route. Without that branch, "light"
// would fall through to LoadScheme, find no light.json, and come back as an empty scheme whose
// Base defaults to Dark — so choosing the light built-in would silently give a dark editor.
TEST_CASE("A built-in name resolves to that built-in without reading a file")
{
	const TempDirectory temp;

	const Graphite::ResolvedScheme resolved = Graphite::ResolveNamed("light", temp.GetPath());

	const std::size_t background = static_cast<std::size_t>(Graphite::EditorColor::Background);

	CHECK(resolved.Editor[background].R ==
		  doctest::Approx(Graphite::LightScheme().Editor[background].R));
}

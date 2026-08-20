#include <doctest/doctest.h>

#include "TempDirectory.h"
#include "Theme/BuiltInSchemes.h"
#include "Theme/ColorScheme.h"
#include "Theme/SchemeStore.h"

#include <algorithm>
#include <fstream>

namespace {

	void WriteFile(const std::filesystem::path& path, std::string_view contents)
	{
		std::ofstream out(path, std::ios::binary);
		out << contents;
	}

	// A colour nothing else in the scheme has. Whole bytes, because a scheme file stores hex
	// pairs and anything finer would come back rounded.
	Core::Color DistinctColor(std::size_t index)
	{
		const float channel = static_cast<float>(index + 1) / 255.0f;

		return Core::Color{channel, channel, channel, 1.0f};
	}

	constexpr std::size_t kSyntaxCount = static_cast<std::size_t>(Graphite::TokenKind::Count);
	constexpr std::size_t kEditorCount = static_cast<std::size_t>(Graphite::EditorColor::Count);
	constexpr std::size_t kChromeCount = static_cast<std::size_t>(Graphite::ChromeColor::Count);

	// The three categories are numbered end to end so that no two colours anywhere in the
	// scheme share a value: a category wired to another one's names or prefix would then show
	// up as the wrong colour rather than as the right one by accident.
	constexpr std::size_t kEditorBase = kSyntaxCount;
	constexpr std::size_t kChromeBase = kSyntaxCount + kEditorCount;
}

// Three categories with parallel but separate machinery — a sparse array, a resolve loop, a
// name, a store prefix with its load branch and its save loop. Missing one of those for one
// category compiles and half-works: an override that loads but never applies, or applies but
// never persists. Saturating all three and running the whole path is what catches that; the
// static_asserts on the built-in tables only guard completeness, not the wiring.
TEST_CASE("Every colour of every category survives save, load and resolve")
{
	const TempDirectory temp;

	Graphite::ColorScheme scheme;
	scheme.Base = Graphite::BuiltInScheme::Dark;

	for (std::size_t index = 0; index < kSyntaxCount; ++index)
	{
		scheme.Syntax[index] = DistinctColor(index);
	}

	for (std::size_t index = 0; index < kEditorCount; ++index)
	{
		scheme.Editor[index] = DistinctColor(kEditorBase + index);
	}

	for (std::size_t index = 0; index < kChromeCount; ++index)
	{
		scheme.Chrome[index] = DistinctColor(kChromeBase + index);
	}

	REQUIRE(Graphite::SaveScheme(temp.GetPath(), "saturated", scheme));

	const Graphite::ColorScheme loaded =
		Graphite::LoadScheme(temp.GetPath() / "saturated.json");

	const Graphite::ResolvedScheme resolved =
		Graphite::Resolve(loaded, Graphite::SchemeFor(loaded.Base));

	for (std::size_t index = 0; index < kSyntaxCount; ++index)
	{
		CAPTURE(index);
		REQUIRE(loaded.Syntax[index].has_value());
		CHECK(resolved.Syntax[index].R == doctest::Approx(DistinctColor(index).R).epsilon(0.001));
	}

	for (std::size_t index = 0; index < kEditorCount; ++index)
	{
		CAPTURE(index);
		REQUIRE(loaded.Editor[index].has_value());
		CHECK(resolved.Editor[index].R ==
			  doctest::Approx(DistinctColor(kEditorBase + index).R).epsilon(0.001));
	}

	for (std::size_t index = 0; index < kChromeCount; ++index)
	{
		CAPTURE(index);
		REQUIRE(loaded.Chrome[index].has_value());
		CHECK(resolved.Chrome[index].R ==
			  doctest::Approx(DistinctColor(kChromeBase + index).R).epsilon(0.001));
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

TEST_CASE("A name that would escape the themes directory is refused")
{
	const TempDirectory temp;

	const Graphite::ColorScheme scheme;

	CHECK_FALSE(Graphite::SaveScheme(temp.GetPath(), "../evil", scheme));
	CHECK_FALSE(Graphite::SaveScheme(temp.GetPath(), "sub/evil", scheme));
	CHECK_FALSE(Graphite::SaveScheme(temp.GetPath(), "sub\\evil", scheme));
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

TEST_CASE("A chrome override survives the round trip")
{
	const TempDirectory temp;

	Graphite::ColorScheme scheme;
	scheme.Chrome[static_cast<std::size_t>(Graphite::ChromeColor::Accent)] =
		Core::Color{1.0f, 0.5f, 0.0f, 1.0f};

	REQUIRE(Graphite::SaveScheme(temp.GetPath(), "mine", scheme));

	const Graphite::ColorScheme loaded = Graphite::LoadScheme(temp.GetPath() / "mine.json");

	REQUIRE(loaded.Chrome[static_cast<std::size_t>(Graphite::ChromeColor::Accent)].has_value());
	CHECK(loaded.Chrome[static_cast<std::size_t>(Graphite::ChromeColor::Accent)]->R ==
		  doctest::Approx(1.0f));
	CHECK(loaded.Chrome[static_cast<std::size_t>(Graphite::ChromeColor::Accent)]->G ==
		  doctest::Approx(0.5f).epsilon(0.01));

	// Sparse, as the other two categories are: a scheme that sets one chrome colour must not
	// come back having claimed the other seven.
	CHECK_FALSE(loaded.Chrome[static_cast<std::size_t>(Graphite::ChromeColor::Border)].has_value());
}

TEST_CASE("A chrome colour is read from the ui prefix a hand-written file uses")
{
	const TempDirectory temp;

	WriteFile(temp.GetPath() / "hand.json", R"({"base":"dark","ui.border":"504945"})");

	const Graphite::ColorScheme loaded = Graphite::LoadScheme(temp.GetPath() / "hand.json");

	REQUIRE(loaded.Chrome[static_cast<std::size_t>(Graphite::ChromeColor::Border)].has_value());
	CHECK(loaded.Chrome[static_cast<std::size_t>(Graphite::ChromeColor::Border)]->R ==
		  doctest::Approx(80.0f / 255.0f).epsilon(0.01));
}

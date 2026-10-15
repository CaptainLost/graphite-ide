#include <doctest/doctest.h>

#include "FileFixtures.h"
#include "TempDirectory.h"
#include "Theme/BuiltInSchemes.h"
#include "Theme/ColorScheme.h"
#include "Theme/SchemeStore.h"

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

namespace {

	Core::Color DistinctColor(std::size_t index)
	{
		const float channel = static_cast<float>(index + 1) / 255.0f;

		return Core::Color{channel, channel, channel, 1.0f};
	}

	constexpr std::size_t kSyntaxCount = static_cast<std::size_t>(Graphite::TokenKind::Count);
	constexpr std::size_t kEditorCount = static_cast<std::size_t>(Graphite::EditorColor::Count);
	constexpr std::size_t kChromeCount = static_cast<std::size_t>(Graphite::ChromeColor::Count);
	constexpr std::size_t kGitCount = static_cast<std::size_t>(Graphite::GitColor::Count);
	constexpr std::size_t kSessionCount = static_cast<std::size_t>(Graphite::SessionColor::Count);

	constexpr std::size_t kEditorBase = kSyntaxCount;
	constexpr std::size_t kChromeBase = kSyntaxCount + kEditorCount;
	constexpr std::size_t kGitBase = kSyntaxCount + kEditorCount + kChromeCount;
	constexpr std::size_t kSessionBase = kSyntaxCount + kEditorCount + kChromeCount + kGitCount;
}

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

	for (std::size_t index = 0; index < kGitCount; ++index)
	{
		scheme.Git[index] = DistinctColor(kGitBase + index);
	}

	for (std::size_t index = 0; index < kSessionCount; ++index)
	{
		scheme.Session[index] = DistinctColor(kSessionBase + index);
	}

	REQUIRE(Graphite::SaveScheme(temp.GetPath(), "saturated", scheme));

	const Graphite::ColorScheme loaded =
		Graphite::LoadScheme(temp.GetPath() / "saturated.json");

	const Graphite::ResolvedScheme resolved =
		Graphite::ResolveSchemeOverBase(loaded, Graphite::SchemeFor(loaded.Base));

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

	for (std::size_t index = 0; index < kGitCount; ++index)
	{
		CAPTURE(index);
		REQUIRE(loaded.Git[index].has_value());
		CHECK(resolved.Git[index].R ==
			  doctest::Approx(DistinctColor(kGitBase + index).R).epsilon(0.001));
	}

	for (std::size_t index = 0; index < kSessionCount; ++index)
	{
		CAPTURE(index);
		REQUIRE(loaded.Session[index].has_value());
		CHECK(resolved.Session[index].R ==
			  doctest::Approx(DistinctColor(kSessionBase + index).R).epsilon(0.001));
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

	WriteFileBytes(temp.GetPath() / "broken.json",
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

	WriteFileBytes(temp.GetPath() / "future.json",
				   R"({"base":"dark","syntax.aardvark":"C586C0","syntax.comment":"6A9955"})");

	const Graphite::ColorScheme loaded =
		Graphite::LoadScheme(temp.GetPath() / "future.json");

	CHECK(loaded.Syntax[static_cast<std::size_t>(Graphite::TokenKind::Comment)].has_value());
}

TEST_CASE("A scheme naming an unknown base falls back to dark and keeps its overrides")
{
	const TempDirectory temp;

	WriteFileBytes(temp.GetPath() / "odd.json",
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
	WriteFileBytes(temp.GetPath() / "dark.json", R"({"base":"dark"})");
	WriteFileBytes(temp.GetPath() / "notes.txt", "ignored");

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

	CHECK_FALSE(loaded.Chrome[static_cast<std::size_t>(Graphite::ChromeColor::Border)].has_value());
}

TEST_CASE("A chrome colour is read from the ui prefix a hand-written file uses")
{
	const TempDirectory temp;

	WriteFileBytes(temp.GetPath() / "hand.json", R"({"base":"dark","ui.border":"504945"})");

	const Graphite::ColorScheme loaded = Graphite::LoadScheme(temp.GetPath() / "hand.json");

	REQUIRE(loaded.Chrome[static_cast<std::size_t>(Graphite::ChromeColor::Border)].has_value());
	CHECK(loaded.Chrome[static_cast<std::size_t>(Graphite::ChromeColor::Border)]->R ==
		  doctest::Approx(80.0f / 255.0f).epsilon(0.01));
}

TEST_CASE("A scheme name is checked for being usable as a file name")
{
	const std::vector<std::string> existing{"dark", "light", "Mine"};

	CHECK_FALSE(Graphite::SchemeNameProblem("ocean", existing).has_value());
	CHECK_FALSE(Graphite::SchemeNameProblem("my scheme-2", existing).has_value());

	CHECK(Graphite::SchemeNameProblem("", existing).has_value());
	CHECK(Graphite::SchemeNameProblem("a/b", existing).has_value());
	CHECK(Graphite::SchemeNameProblem("a\\b", existing).has_value());
	CHECK(Graphite::SchemeNameProblem("a:b", existing).has_value());
	CHECK(Graphite::SchemeNameProblem("what?", existing).has_value());
	CHECK(Graphite::SchemeNameProblem("a..b", existing).has_value());
	CHECK(Graphite::SchemeNameProblem("trailing.", existing).has_value());
	CHECK(Graphite::SchemeNameProblem("trailing ", existing).has_value());
	CHECK(Graphite::SchemeNameProblem(" leading", existing).has_value());
	CHECK(Graphite::SchemeNameProblem("tab\tinside", existing).has_value());
	CHECK(Graphite::SchemeNameProblem("CON", existing).has_value());
	CHECK(Graphite::SchemeNameProblem("lpt3", existing).has_value());
	CHECK_FALSE(Graphite::SchemeNameProblem("lpt10", existing).has_value());
	CHECK(Graphite::SchemeNameProblem("nul.dark", existing).has_value());
	CHECK(Graphite::SchemeNameProblem("COM0", existing).has_value());
	CHECK(Graphite::SchemeNameProblem("conin$", existing).has_value());
	CHECK(Graphite::SchemeNameProblem(std::string(65, 'x'), existing).has_value());
	CHECK_FALSE(Graphite::SchemeNameProblem(std::string(64, 'x'), existing).has_value());
}

TEST_CASE("A scheme name that is taken is refused whatever its case")
{
	const std::vector<std::string> existing{"dark", "light", "Mine"};

	CHECK(Graphite::SchemeNameProblem("mine", existing).has_value());
	CHECK(Graphite::SchemeNameProblem("MINE", existing).has_value());
	CHECK(Graphite::SchemeNameProblem("Dark", existing).has_value());
}

TEST_CASE("Renaming a scheme moves its file and keeps its colours")
{
	const TempDirectory temp;

	Graphite::ColorScheme scheme;
	scheme.Syntax[0] = DistinctColor(0);
	REQUIRE(Graphite::SaveScheme(temp.GetPath(), "old", scheme));

	REQUIRE(Graphite::RenameScheme(temp.GetPath(), "old", "new"));

	CHECK_FALSE(std::filesystem::exists(temp.GetPath() / "old.json"));
	REQUIRE(std::filesystem::exists(temp.GetPath() / "new.json"));

	const Graphite::ColorScheme loaded = Graphite::LoadScheme(temp.GetPath() / "new.json");
	REQUIRE(loaded.Syntax[0].has_value());
	CHECK(loaded.Syntax[0]->R == doctest::Approx(DistinctColor(0).R));
}

TEST_CASE("Renaming a scheme refuses a taken name, a missing source, a built-in and a bad name")
{
	const TempDirectory temp;

	const Graphite::ColorScheme scheme;
	REQUIRE(Graphite::SaveScheme(temp.GetPath(), "one", scheme));
	REQUIRE(Graphite::SaveScheme(temp.GetPath(), "two", scheme));

	CHECK_FALSE(Graphite::RenameScheme(temp.GetPath(), "one", "two"));
	CHECK_FALSE(Graphite::RenameScheme(temp.GetPath(), "ghost", "three"));
	CHECK_FALSE(Graphite::RenameScheme(temp.GetPath(), "one", "dark"));
	CHECK_FALSE(Graphite::RenameScheme(temp.GetPath(), "dark", "three"));
	CHECK_FALSE(Graphite::RenameScheme(temp.GetPath(), "one", "../escape"));

	CHECK(std::filesystem::exists(temp.GetPath() / "one.json"));
	CHECK(std::filesystem::exists(temp.GetPath() / "two.json"));
}

TEST_CASE("A scheme with a non-ASCII name is saved, renamed, listed and loaded under that name")
{
	const TempDirectory temp;

	const std::string firstPolishName = "Ciemny \xC5\xBC";
	const std::string secondPolishName = "\xC5\xBB\xC3\xB3\xC5\x82ty";

	Graphite::ColorScheme scheme;
	scheme.Syntax[0] = DistinctColor(0);
	REQUIRE(Graphite::SaveScheme(temp.GetPath(), firstPolishName, scheme));
	REQUIRE(std::filesystem::exists(Graphite::SchemeFile(temp.GetPath(), firstPolishName)));

	REQUIRE(Graphite::RenameScheme(temp.GetPath(), firstPolishName, secondPolishName));
	CHECK_FALSE(std::filesystem::exists(Graphite::SchemeFile(temp.GetPath(), firstPolishName)));

	const std::vector<std::string> names = Graphite::ListSchemes(temp.GetPath());
	REQUIRE(names.size() == 1);
	CHECK(names[0] == secondPolishName);

	const Graphite::ColorScheme loaded = Graphite::LoadScheme(Graphite::SchemeFile(temp.GetPath(), names[0]));
	REQUIRE(loaded.Syntax[0].has_value());

	REQUIRE(Graphite::DeleteScheme(temp.GetPath(), secondPolishName));
	CHECK(Graphite::ListSchemes(temp.GetPath()).empty());
}

TEST_CASE("The name length limit counts characters, not bytes")
{
	std::string name;

	for (int character = 0; character < 64; ++character)
	{
		name += "\xC5\xBC";
	}

	CHECK_FALSE(Graphite::SchemeNameProblem(name, {}).has_value());
	CHECK(Graphite::SchemeNameProblem(name + "x", {}).has_value());
}

TEST_CASE("A scheme can be renamed to the same name in another case")
{
	const TempDirectory temp;

	REQUIRE(Graphite::SaveScheme(temp.GetPath(), "mine", Graphite::ColorScheme{}));
	REQUIRE(Graphite::RenameScheme(temp.GetPath(), "mine", "Mine"));

	const std::vector<std::string> names = Graphite::ListSchemes(temp.GetPath());
	REQUIRE(names.size() == 1);
	CHECK(names[0] == "Mine");
}

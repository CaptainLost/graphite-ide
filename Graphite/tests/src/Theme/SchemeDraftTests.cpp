#include <doctest/doctest.h>

#include "FileFixtures.h"
#include "TempDirectory.h"
#include "Theme/BuiltInSchemes.h"
#include "Theme/SchemeDraft.h"
#include "Theme/SchemeStore.h"
#include "UI/ToastEvent.h"

#include <algorithm>
#include <filesystem>
#include <optional>
#include <string>

namespace {

	constexpr std::size_t kCommentIndex = static_cast<std::size_t>(Graphite::TokenKind::Comment);
	constexpr std::size_t kKeywordIndex = static_cast<std::size_t>(Graphite::TokenKind::Keyword);

	constexpr Core::Color kEightBitExactColor{0.2f, 0.4f, 0.8f, 1.0f};
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

TEST_CASE("An empty name, as in a first run, selects the dark built-in and not a file called .json")
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
	draft.SetColor(Graphite::TokenKind::Comment, kEightBitExactColor);

	CHECK_FALSE(draft.IsOverridden(Graphite::TokenKind::Comment));
	CHECK(draft.GetPreview().Syntax[kCommentIndex].R ==
		  doctest::Approx(Graphite::DarkScheme().Syntax[kCommentIndex].R));
	CHECK_FALSE(draft.Save());
}

TEST_CASE("Editing one colour overrides exactly one colour and saves it sparsely")
{
	const TempDirectory temp;

	Graphite::SchemeDraft draft(temp.GetPath());

	draft.Select("dark");
	REQUIRE(draft.Duplicate());
	REQUIRE_FALSE(draft.IsReadOnly());

	draft.SetColor(Graphite::TokenKind::Comment, kEightBitExactColor);

	CHECK(draft.IsOverridden(Graphite::TokenKind::Comment));
	CHECK_FALSE(draft.IsOverridden(Graphite::TokenKind::Keyword));
	CHECK(draft.GetPreview().Syntax[kCommentIndex].B == doctest::Approx(0.8f));

	REQUIRE(draft.Save());

	const Graphite::ColorScheme saved =
		Graphite::LoadScheme(temp.GetPath() / (draft.GetSelected() + ".json"));

	CHECK(saved.Syntax[kCommentIndex].has_value());
	CHECK_FALSE(saved.Syntax[kKeywordIndex].has_value());
}

TEST_CASE("Reverting clears the override rather than pinning the base value")
{
	const TempDirectory temp;

	Graphite::SchemeDraft draft(temp.GetPath());

	draft.Select("dark");
	REQUIRE(draft.Duplicate());

	draft.SetColor(Graphite::TokenKind::Comment, kEightBitExactColor);
	REQUIRE(draft.IsOverridden(Graphite::TokenKind::Comment));

	draft.Revert(Graphite::TokenKind::Comment);

	CHECK_FALSE(draft.IsOverridden(Graphite::TokenKind::Comment));
	CHECK(draft.GetPreview().Syntax[kCommentIndex].R ==
		  doctest::Approx(Graphite::DarkScheme().Syntax[kCommentIndex].R));

	REQUIRE(draft.Save());

	const Graphite::ColorScheme saved =
		Graphite::LoadScheme(temp.GetPath() / (draft.GetSelected() + ".json"));

	CHECK_FALSE(saved.Syntax[kCommentIndex].has_value());
}

TEST_CASE("A duplicate never lands on a name already taken")
{
	const TempDirectory temp;

	Graphite::SchemeDraft draft(temp.GetPath());

	draft.Select("dark");
	REQUIRE(draft.Duplicate());

	const std::string firstDuplicateName = draft.GetSelected();

	draft.Select("dark");
	REQUIRE(draft.Duplicate());

	CHECK(draft.GetSelected() != firstDuplicateName);
	CHECK(Graphite::UniqueSchemeName(temp.GetPath(), "dark") != firstDuplicateName);
	CHECK(Graphite::UniqueSchemeName(temp.GetPath(), "dark") != draft.GetSelected());
}

TEST_CASE("A duplicate carries the source scheme's overrides")
{
	const TempDirectory temp;

	Graphite::ColorScheme source;
	source.Base = Graphite::BuiltInScheme::Light;
	source.Syntax[kCommentIndex] = kEightBitExactColor;

	REQUIRE(Graphite::SaveScheme(temp.GetPath(), "source", source));

	Graphite::SchemeDraft draft(temp.GetPath());

	draft.Select("source");
	REQUIRE(draft.Duplicate());

	CHECK(draft.IsOverridden(Graphite::TokenKind::Comment));
	CHECK(draft.GetPreview().Syntax[kCommentIndex].B == doctest::Approx(0.8f));

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

TEST_CASE("Deleting a built-in is refused even when a file by its name is on disk")
{
	const TempDirectory temp;

	const std::filesystem::path shadow = temp.GetPath() / "dark.json";

	WriteFileBytes(shadow, "{\"base\":\"dark\"}");
	REQUIRE(std::filesystem::exists(shadow));

	Graphite::SchemeDraft draft(temp.GetPath());

	draft.Select("dark");

	CHECK_FALSE(draft.DeleteSelected());
	CHECK(draft.GetSelected() == "dark");
	CHECK(std::filesystem::exists(shadow));
}

TEST_CASE("Renaming the selected scheme keeps it selected under the new name")
{
	const TempDirectory temp;

	Graphite::ColorScheme source;
	REQUIRE(Graphite::SaveScheme(temp.GetPath(), "before", source));

	Graphite::SchemeDraft draft(temp.GetPath());

	draft.Select("before");
	draft.SetColor(Graphite::TokenKind::Comment, kEightBitExactColor);
	REQUIRE(draft.Save());

	REQUIRE(draft.RenameSelected("after"));

	CHECK(draft.GetSelected() == "after");
	CHECK(draft.IsOverridden(Graphite::TokenKind::Comment));
	CHECK(std::find(draft.GetNames().begin(), draft.GetNames().end(), "before") == draft.GetNames().end());
	CHECK(std::find(draft.GetNames().begin(), draft.GetNames().end(), "after") != draft.GetNames().end());
	CHECK_FALSE(std::filesystem::exists(temp.GetPath() / "before.json"));
}

TEST_CASE("Renaming is refused for a built-in, a taken name and an unusable name")
{
	const TempDirectory temp;

	const Graphite::ColorScheme source;
	REQUIRE(Graphite::SaveScheme(temp.GetPath(), "first", source));
	REQUIRE(Graphite::SaveScheme(temp.GetPath(), "second", source));

	Graphite::SchemeDraft draft(temp.GetPath());

	draft.Select("dark");
	CHECK_FALSE(draft.RenameSelected("renamed"));
	CHECK(draft.GetSelected() == "dark");

	draft.Select("first");
	CHECK_FALSE(draft.RenameSelected("second"));
	CHECK_FALSE(draft.RenameSelected("first"));
	CHECK_FALSE(draft.RenameSelected("a/b"));
	CHECK_FALSE(draft.RenameSelected(""));
	CHECK(draft.GetSelected() == "first");
	CHECK(std::filesystem::exists(temp.GetPath() / "first.json"));
}

TEST_CASE("A chrome colour can be overridden and reverted in a draft")
{
	const TempDirectory temp;

	Graphite::SchemeDraft draft(temp.GetPath());
	REQUIRE(draft.Duplicate());

	constexpr std::size_t accent = static_cast<std::size_t>(Graphite::ChromeColor::Accent);

	CHECK_FALSE(draft.IsOverridden(Graphite::ChromeColor::Accent));

	draft.SetColor(Graphite::ChromeColor::Accent, Core::Color{1.0f, 0.5f, 0.0f, 1.0f});

	CHECK(draft.IsOverridden(Graphite::ChromeColor::Accent));
	CHECK(draft.GetPreview().Chrome[accent].R == doctest::Approx(1.0f));

	draft.Revert(Graphite::ChromeColor::Accent);

	CHECK_FALSE(draft.IsOverridden(Graphite::ChromeColor::Accent));
	CHECK(draft.GetPreview().Chrome[accent].R ==
		  doctest::Approx(Graphite::DarkScheme().Chrome[accent].R));
}

TEST_CASE("The selected scheme can be renamed to a non-ASCII name and to its own name in another case")
{
	const TempDirectory temp;

	REQUIRE(Graphite::SaveScheme(temp.GetPath(), "plain", Graphite::ColorScheme{}));

	Graphite::SchemeDraft draft(temp.GetPath());
	draft.Select("plain");

	const std::string polishName = "Ciemny \xC5\xBC";
	REQUIRE(draft.RenameSelected(polishName));
	CHECK(draft.GetSelected() == polishName);
	CHECK(std::filesystem::exists(Graphite::SchemeFile(temp.GetPath(), polishName)));

	CHECK_FALSE(draft.NameProblem("ciemny \xC5\xBC").has_value());
	CHECK_FALSE(draft.RenameSelected(polishName));
}

TEST_CASE("A scheme that cannot be deleted leaves one failure toast to take")
{
	const TempDirectory temp;

	REQUIRE(Graphite::SaveScheme(temp.GetPath(), "doomed", Graphite::ColorScheme{}));

	Graphite::SchemeDraft draft(temp.GetPath());
	draft.Select("doomed");

	std::filesystem::remove(temp.GetPath() / "doomed.json");

	CHECK_FALSE(draft.DeleteSelected());

	const std::optional<Graphite::ToastEvent> failure = draft.TakeFailure();

	REQUIRE(failure.has_value());
	CHECK(failure->Kind == Core::ToastKind::Error);
	CHECK(failure->Title == "Could not delete scheme");
	CHECK(failure->Message.find("doomed") != std::string::npos);
	CHECK(failure->Scope == Graphite::ToastScope::Application);

	CHECK_FALSE(draft.TakeFailure().has_value());
}

TEST_CASE("A scheme that cannot be saved leaves one failure toast to take")
{
	const TempDirectory temp;

	REQUIRE(Graphite::SaveScheme(temp.GetPath(), "mine", Graphite::ColorScheme{}));

	Graphite::SchemeDraft draft(temp.GetPath());
	draft.Select("mine");
	draft.SetColor(Graphite::TokenKind::Comment, kEightBitExactColor);

	std::filesystem::remove(temp.GetPath() / "mine.json");
	std::filesystem::create_directory(temp.GetPath() / "mine.json");

	CHECK_FALSE(draft.Save());

	const std::optional<Graphite::ToastEvent> failure = draft.TakeFailure();

	REQUIRE(failure.has_value());
	CHECK(failure->Title == "Could not save scheme");
	CHECK(failure->Scope == Graphite::ToastScope::Application);
}

TEST_CASE("A draft whose every action succeeded has no failure to take")
{
	const TempDirectory temp;

	Graphite::SchemeDraft draft(temp.GetPath());

	REQUIRE(draft.Duplicate());
	REQUIRE(draft.Save());
	REQUIRE(draft.DeleteSelected());

	CHECK_FALSE(draft.TakeFailure().has_value());
}

TEST_CASE("Two failures before the section drains are handed out as two toasts, oldest first")
{
	const TempDirectory temp;

	REQUIRE(Graphite::SaveScheme(temp.GetPath(), "mine", Graphite::ColorScheme{}));

	Graphite::SchemeDraft draft(temp.GetPath());
	draft.Select("mine");

	std::filesystem::remove(temp.GetPath() / "mine.json");
	std::filesystem::create_directory(temp.GetPath() / "mine.json");
	WriteFileBytes(temp.GetPath() / "mine.json" / "keep", "x");

	CHECK_FALSE(draft.Save());
	CHECK_FALSE(draft.DeleteSelected());

	const std::optional<Graphite::ToastEvent> saveFailure = draft.TakeFailure();
	const std::optional<Graphite::ToastEvent> deleteFailure = draft.TakeFailure();

	REQUIRE(saveFailure.has_value());
	REQUIRE(deleteFailure.has_value());
	CHECK(saveFailure->Title == "Could not save scheme");
	CHECK(deleteFailure->Title == "Could not delete scheme");
	CHECK_FALSE(draft.TakeFailure().has_value());
}

#include <doctest/doctest.h>

#include "SettingsPageFrames.h"
#include "TempAppSettings.h"
#include "TempDirectory.h"

#include "Settings/AppSettings.h"
#include "Settings/Panel/AppearancePage.h"
#include "Settings/Panel/SettingsSearch.h"
#include "Theme/ActiveScheme.h"
#include "Theme/ColorScheme.h"
#include "UI/ModalGate.h"

#include "Core/Events/EventBus.h"

#include <imgui.h>

#include <algorithm>
#include <cstddef>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

	// With an own scheme, "mine" is written with one overridden colour and chosen before the page is built.
	struct AppearanceParts
	{
		explicit AppearanceParts(bool withOwnScheme)
		{
			if (withOwnScheme)
			{
				std::ofstream file(Themes.GetPath() / "mine.json");
				file << R"({"base":"dark","syntax.comment":"6A9955"})";
				file.close();
				Settings.Get().SetColorScheme("mine");
			}
		}

		TempDirectory Themes;
		TempAppSettings Settings;
	};

	struct AppearanceScreen : AppearanceParts
	{
		explicit AppearanceScreen(bool withOwnScheme = false) : AppearanceParts(withOwnScheme) {}

		Graphite::ActiveScheme Scheme;
		Graphite::ModalGate Modals;
		Core::EventBus Events;
		Graphite::AppearancePage Page{Scheme, Settings.Get(), Modals, Events, Themes.GetPath()};
		SettingsPageFrames Frames{Page};
	};

	constexpr std::size_t kThemeAndInterfaceCards = 2;

	std::vector<Graphite::SettingsRow> FindInFrame(AppearanceScreen& screen, const Graphite::SettingsRow& block, std::string_view query)
	{
		std::vector<Graphite::SettingsRow> found;
		const std::vector<std::string> words = Graphite::SplitQueryWords(query);
		screen.Frames.GetFrames().Next([&found, &block, &words] { found = block.FindInside(words); });

		return found;
	}

	void DrawControlInFrame(AppearanceScreen& screen, const Graphite::SettingsRow& row)
	{
		screen.Frames.GetFrames().Next([&row] {
			ImGui::Begin("Found");
			ImGui::PushID(row.Key.c_str());
			row.DrawControl();
			ImGui::PopID();
			ImGui::End();
		});
	}
}

TEST_CASE("The appearance page holds the theme, the interface and a card for each group of colours")
{
	AppearanceScreen screen;

	CHECK(screen.Frames.CardHeadings() == std::vector<std::string>{"Theme", "Interface", "Syntax colours", "Editor colours",
																   "Application colours", "Git colours", "Claude sessions colours"});
}

TEST_CASE("Each group of colours is one block row under the theme and the interface")
{
	AppearanceScreen screen;

	CHECK(screen.Frames.RowTitles() == std::vector<std::string>{"Colour scheme", "This scheme", "Interface scale", "Syntax colours",
																"Editor colours", "Application colours", "Git colours",
																"Claude sessions colours"});

	const std::vector<Graphite::SettingsCard> cards = screen.Frames.DescribeInFrame();

	for (std::size_t cardIndex = kThemeAndInterfaceCards; cardIndex < cards.size(); ++cardIndex)
	{
		REQUIRE(cards[cardIndex].Rows.size() == 1);
		CHECK(cards[cardIndex].Rows.front().IsBlock());
	}
}

TEST_CASE("A colour is found by its name through its group's block, as a row of its own")
{
	AppearanceScreen screen;

	const std::optional<Graphite::SettingsRow> block = screen.Frames.FindRow("Syntax colours");

	REQUIRE(block.has_value());
	REQUIRE(block->FindInside);

	const std::vector<Graphite::SettingsRow> comment = FindInFrame(screen, *block, "comment");
	REQUIRE(comment.size() == 1);
	CHECK(comment[0].Title == "Comment");
	CHECK(comment[0].Key == "Comment");
	CHECK(comment[0].Description.empty());
	CHECK(comment[0].DrawControl);

	CHECK(FindInFrame(screen, *block, "COMMENT").size() == 1);
	CHECK(FindInFrame(screen, *block, "zzz").empty());
	CHECK(FindInFrame(screen, *block, "semantic").size() == 6);
	CHECK(FindInFrame(screen, *block, "syntax comment").empty());
}

TEST_CASE("An own scheme's colour cards explain dimmed names and the reset icon")
{
	AppearanceScreen screen;

	const std::vector<Graphite::SettingsCard> builtInCards = screen.Frames.DescribeInFrame();

	for (std::size_t cardIndex = kThemeAndInterfaceCards; cardIndex < builtInCards.size(); ++cardIndex)
	{
		CHECK(builtInCards[cardIndex].Note.empty());
	}

	screen.Frames.ClickControl("This scheme");

	const std::vector<Graphite::SettingsCard> cards = screen.Frames.DescribeInFrame();

	for (std::size_t cardIndex = kThemeAndInterfaceCards; cardIndex < cards.size(); ++cardIndex)
	{
		CHECK(cards[cardIndex].Note == "Dimmed names are inherited from the built-in scheme. \xE2\x86\xBA puts an overridden colour back.");
	}
}

TEST_CASE("A built-in scheme says it cannot be edited")
{
	AppearanceScreen screen;

	const std::optional<Graphite::SettingsRow> row = screen.Frames.FindRow("This scheme");

	REQUIRE(row.has_value());
	CHECK(row->Description == "Built-in schemes cannot be edited. Duplicate one to change it.");
}

TEST_CASE("Duplicating a built-in scheme chooses an editable copy")
{
	AppearanceScreen screen;
	const std::string before = screen.Settings.Get().GetColorScheme();

	screen.Frames.ClickControl("This scheme");

	CHECK(screen.Settings.Get().GetColorScheme() != before);
	const std::optional<Graphite::SettingsRow> row = screen.Frames.FindRow("This scheme");
	REQUIRE(row.has_value());
	CHECK(row->Description == "A copy of your own: rename it, delete it, or change its colours below.");
}

TEST_CASE("The appearance page draws wide and narrow, built-in and own, without an ImGui assertion")
{
	AppearanceScreen screen;

	screen.Frames.DrawPageTwice(900.0f);
	screen.Frames.DrawPageTwice(320.0f);
	screen.Frames.ClickControl("This scheme");
	screen.Frames.DrawPageTwice(900.0f);

	CHECK_FALSE(screen.Modals.IsBusy());
}

TEST_CASE("A colour card's block is found by the colour names it holds")
{
	AppearanceScreen screen;

	const std::optional<Graphite::SettingsRow> block = screen.Frames.FindRow("Syntax colours");

	REQUIRE(block.has_value());
	CHECK(std::find(block->Keywords.begin(), block->Keywords.end(), "Comment") != block->Keywords.end());
}

TEST_CASE("An own scheme with an overridden colour draws wide and narrow without an ImGui assertion")
{
	AppearanceScreen screen(true);

	screen.Frames.DrawPageTwice(900.0f);
	screen.Frames.DrawPageTwice(320.0f);

	CHECK_FALSE(screen.Modals.IsBusy());
}

TEST_CASE("In an own scheme an inherited colour's row says so and an overridden one's does not")
{
	AppearanceScreen screen(true);

	const std::optional<Graphite::SettingsRow> block = screen.Frames.FindRow("Syntax colours");
	REQUIRE(block.has_value());

	const std::vector<Graphite::SettingsRow> comment = FindInFrame(screen, *block, "comment");
	const std::vector<Graphite::SettingsRow> keyword = FindInFrame(screen, *block, "keyword");

	REQUIRE(comment.size() == 1);
	CHECK(comment[0].Description.empty());
	REQUIRE_FALSE(keyword.empty());
	CHECK(keyword[0].Description == "Inherited from the built-in scheme.");
}

TEST_CASE("A found colour row's control is as wide as the swatch, and wider in an own scheme for the reset icon")
{
	AppearanceScreen builtIn;
	AppearanceScreen own(true);

	const std::optional<Graphite::SettingsRow> builtInBlock = builtIn.Frames.FindRow("Syntax colours");
	const std::optional<Graphite::SettingsRow> ownBlock = own.Frames.FindRow("Syntax colours");
	REQUIRE(builtInBlock.has_value());
	REQUIRE(ownBlock.has_value());

	const std::vector<Graphite::SettingsRow> builtInRows = FindInFrame(builtIn, *builtInBlock, "comment");
	const std::vector<Graphite::SettingsRow> ownRows = FindInFrame(own, *ownBlock, "comment");

	REQUIRE(builtInRows.size() == 1);
	REQUIRE(ownRows.size() == 1);
	CHECK(builtInRows[0].ControlWidth > 0.0f);
	CHECK(ownRows[0].ControlWidth > builtInRows[0].ControlWidth);
}

TEST_CASE("A found colour row's control draws, built-in and own, overridden and inherited, without an ImGui assertion")
{
	AppearanceScreen builtIn;
	AppearanceScreen own(true);

	for (AppearanceScreen* screen : {&builtIn, &own})
	{
		const std::optional<Graphite::SettingsRow> block = screen->Frames.FindRow("Syntax colours");
		REQUIRE(block.has_value());

		for (const char* query : {"comment", "keyword"})
		{
			const std::vector<Graphite::SettingsRow> rows = FindInFrame(*screen, *block, query);
			REQUIRE_FALSE(rows.empty());

			DrawControlInFrame(*screen, rows[0]);
			DrawControlInFrame(*screen, rows[0]);
		}
	}

	CHECK_FALSE(own.Modals.IsBusy());
}

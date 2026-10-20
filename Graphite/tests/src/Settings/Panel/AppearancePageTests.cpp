#include <doctest/doctest.h>

#include "SettingsPageFrames.h"
#include "TempAppSettings.h"
#include "TempDirectory.h"

#include "Settings/AppSettings.h"
#include "Settings/Panel/AppearancePage.h"
#include "Settings/Panel/SettingsSearch.h"
#include "Theme/ActiveScheme.h"
#include "Theme/BuiltInSchemes.h"
#include "Theme/ChromePalette.h"
#include "Theme/ColorScheme.h"
#include "UI/Dialogs/ModalGate.h"

#include "Core/App/Redraw.h"
#include "Core/Events/EventBus.h"
#include "Core/ImGui/Theme.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cstddef>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

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

	constexpr std::size_t kLayoutCard = 2;
	constexpr std::size_t kCardsBeforeColours = 3;

	constexpr float kNarrowWidth = 400.0f;

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

TEST_CASE("The appearance page holds the theme, the interface, the layout and a card for each group of colours")
{
	AppearanceScreen screen;

	CHECK(screen.Frames.CardHeadings() == std::vector<std::string>{"Theme", "Interface", "Layout", "Syntax colours",
																   "Editor colours", "Application colours", "Git colours",
																   "Claude sessions colours"});
}

TEST_CASE("Each group of colours is one block row under the theme, the interface and the layout")
{
	AppearanceScreen screen;

	CHECK(screen.Frames.RowTitles() == std::vector<std::string>{"Colour scheme", "This scheme", "Interface scale", "Gap between panels",
																"Panel corner radius", "Control corner radius", "Border width",
																"Scrollbar width", "Syntax colours", "Editor colours", "Application colours",
																"Git colours", "Claude sessions colours"});

	const std::vector<Graphite::SettingsCard> cards = screen.Frames.DescribeInFrame();

	for (std::size_t cardIndex = kCardsBeforeColours; cardIndex < cards.size(); ++cardIndex)
	{
		REQUIRE(cards[cardIndex].Rows.size() == 1);
		CHECK(cards[cardIndex].Rows.front().IsBlock());
	}
}

TEST_CASE("The layout card has a slider row for each measurement, and a built-in's are for looking at")
{
	AppearanceScreen screen;

	const std::vector<Graphite::SettingsCard> cards = screen.Frames.DescribeInFrame();

	REQUIRE(cards.size() > kLayoutCard);
	CHECK(cards[kLayoutCard].Rows.size() == Graphite::kLayoutMetricCount);

	for (const Graphite::SettingsRow& row : cards[kLayoutCard].Rows)
	{
		CHECK_FALSE(row.IsBlock());
		CHECK(row.DrawControl);
		CHECK(row.Description == "Built-in scheme: duplicate it to change this measurement.");
	}
}

TEST_CASE("A copy's layout rows are dimmed until the copy sets them, and the card says what the reset does")
{
	AppearanceScreen screen;

	screen.Frames.ClickControl("This scheme");

	const std::vector<Graphite::SettingsCard> cards = screen.Frames.DescribeInFrame();

	REQUIRE(cards.size() > kLayoutCard);
	CHECK(cards[kLayoutCard].Note == "Dimmed names are inherited from the built-in scheme. âº puts an overridden measurement back.");

	for (const Graphite::SettingsRow& row : cards[kLayoutCard].Rows)
	{
		CHECK(row.DimmedTitle);
		CHECK(row.Description.empty());
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
	CHECK(comment[0].DrawControl);
	CHECK_FALSE(comment[0].OpenWith.empty());

	CHECK(FindInFrame(screen, *block, "COMMENT").size() == 1);
	CHECK(FindInFrame(screen, *block, "zzz").empty());
	CHECK(FindInFrame(screen, *block, "semantic").size() == 6);
	CHECK(FindInFrame(screen, *block, "syntax comment").empty());
}

TEST_CASE("An own scheme's colour cards explain dimmed names and the reset icon")
{
	AppearanceScreen screen;

	const std::vector<Graphite::SettingsCard> builtInCards = screen.Frames.DescribeInFrame();

	for (std::size_t cardIndex = kCardsBeforeColours; cardIndex < builtInCards.size(); ++cardIndex)
	{
		CHECK(builtInCards[cardIndex].Note.empty());
	}

	screen.Frames.ClickControl("This scheme");

	const std::vector<Graphite::SettingsCard> cards = screen.Frames.DescribeInFrame();

	for (std::size_t cardIndex = kCardsBeforeColours; cardIndex < cards.size(); ++cardIndex)
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

TEST_CASE("In an own scheme an inherited colour's row is dimmed and an overridden one's is not")
{
	AppearanceScreen screen(true);

	const std::optional<Graphite::SettingsRow> block = screen.Frames.FindRow("Syntax colours");
	REQUIRE(block.has_value());

	const std::vector<Graphite::SettingsRow> comment = FindInFrame(screen, *block, "comment");
	const std::vector<Graphite::SettingsRow> keyword = FindInFrame(screen, *block, "keyword");

	REQUIRE(comment.size() == 1);
	CHECK_FALSE(comment[0].DimmedTitle);
	CHECK(comment[0].Description.empty());
	REQUIRE_FALSE(keyword.empty());
	CHECK(keyword[0].DimmedTitle);
	CHECK(keyword[0].Description.empty());
}

TEST_CASE("A found colour of a built-in scheme says how to change it, and one of an own scheme says nothing")
{
	AppearanceScreen builtIn;
	AppearanceScreen own(true);

	const std::optional<Graphite::SettingsRow> builtInBlock = builtIn.Frames.FindRow("Syntax colours");
	const std::optional<Graphite::SettingsRow> ownBlock = own.Frames.FindRow("Syntax colours");
	REQUIRE(builtInBlock.has_value());
	REQUIRE(ownBlock.has_value());

	const std::vector<Graphite::SettingsRow> builtInRows = FindInFrame(builtIn, *builtInBlock, "string");
	const std::vector<Graphite::SettingsRow> ownRows = FindInFrame(own, *ownBlock, "string");

	REQUIRE(builtInRows.size() == 1);
	REQUIRE(ownRows.size() == 1);
	CHECK(builtInRows[0].Description == "Built-in scheme: duplicate it to change this colour.");
	CHECK(ownRows[0].Description.empty());
}

TEST_CASE("Colours of one name in two groups are opened with different words")
{
	AppearanceScreen screen;

	const std::optional<Graphite::SettingsRow> syntax = screen.Frames.FindRow("Syntax colours");
	const std::optional<Graphite::SettingsRow> editor = screen.Frames.FindRow("Editor colours");
	REQUIRE(syntax.has_value());
	REQUIRE(editor.has_value());

	const std::vector<Graphite::SettingsRow> syntaxText = FindInFrame(screen, *syntax, "text");
	const std::vector<Graphite::SettingsRow> editorText = FindInFrame(screen, *editor, "text");
	REQUIRE_FALSE(syntaxText.empty());
	REQUIRE_FALSE(editorText.empty());
	REQUIRE(syntaxText[0].Title == "Text");
	REQUIRE(editorText[0].Title == "Text");

	CHECK(syntaxText[0].OpenWith != editorText[0].OpenWith);
}

namespace {

	enum class PageBefore
	{
		NeverDrawn,
		Drawn
	};

	std::string OpenWithForAccentHalfwayDownThePage(AppearanceScreen& screen)
	{
		const std::optional<Graphite::SettingsRow> block = screen.Frames.FindRow("Application colours");
		REQUIRE(block.has_value());
		const std::vector<Graphite::SettingsRow> found = FindInFrame(screen, *block, "accent");
		REQUIRE(found.size() == 1);

		return found[0].OpenWith;
	}

	float ScrollAfterOpeningAccent(PageBefore pageBefore)
	{
		AppearanceScreen screen;

		if (pageBefore == PageBefore::Drawn)
		{
			screen.Frames.DrawPageTwice(kNarrowWidth);
		}

		screen.Page.OnOpenedFromSearch(OpenWithForAccentHalfwayDownThePage(screen));
		screen.Frames.DrawPageTwice(kNarrowWidth);
		screen.Frames.DrawPageTwice(kNarrowWidth);

		const ImGuiWindow* window = ImGui::FindWindowByName("Settings");
		REQUIRE(window != nullptr);
		CHECK(window->Scroll.y < window->ScrollMax.y);

		return window->Scroll.y;
	}

	constexpr int kFramesTheLightLasts = 90;
}

TEST_CASE("Opening a found colour scrolls the page to it, whether or not the page was drawn before")
{
	const float afterDrawing = ScrollAfterOpeningAccent(PageBefore::Drawn);
	const float fresh = ScrollAfterOpeningAccent(PageBefore::NeverDrawn);

	CHECK(afterDrawing > 0.0f);
	CHECK(fresh == doctest::Approx(afterDrawing));
}

TEST_CASE("Opening the page with words that name no colour does not scroll it")
{
	AppearanceScreen screen;

	screen.Page.OnOpenedFromSearch("background");
	screen.Frames.DrawPageTwice(kNarrowWidth);
	screen.Frames.DrawPageTwice(kNarrowWidth);

	const ImGuiWindow* window = ImGui::FindWindowByName("Settings");
	REQUIRE(window != nullptr);
	CHECK(window->Scroll.y == 0.0f);
}

namespace {

	// The light is the accent at part opacity; the accent's own swatch is opaque, so it does not count.
	bool AnyWindowDrawsTheAccentTranslucent()
	{
		const ImU32 accentWithoutAlpha = ImGui::GetColorU32(ImGuiCol_CheckMark) & ~IM_COL32_A_MASK;

		for (const ImGuiWindow* window : ImGui::GetCurrentContext()->Windows)
		{
			for (const ImDrawVert& vertex : window->DrawList->VtxBuffer)
			{
				const ImU32 alpha = (vertex.col & IM_COL32_A_MASK) >> IM_COL32_A_SHIFT;
				const bool translucent = alpha > 0u && alpha < 255u;

				if (translucent && (vertex.col & ~IM_COL32_A_MASK) == accentWithoutAlpha)
				{
					return true;
				}
			}
		}

		return false;
	}
}

TEST_CASE("A found colour is lit in the accent, though the theme gives a press no colour of its own")
{
	AppearanceScreen screen;
	Core::Theme::Apply(Graphite::ChromePaletteFor(Graphite::DarkScheme()));
	const std::string openWith = OpenWithForAccentHalfwayDownThePage(screen);

	screen.Page.OnOpenedFromSearch(openWith);
	screen.Frames.DrawPageTwice(kNarrowWidth);
	screen.Frames.DrawPageTwice(kNarrowWidth);

	CHECK(AnyWindowDrawsTheAccentTranslucent());
}

TEST_CASE("A found colour asks for the next frame while its light fades, and for none once it has")
{
	AppearanceScreen screen;
	const std::string openWith = OpenWithForAccentHalfwayDownThePage(screen);
	static_cast<void>(Core::Redraw::TakeDeadline());

	screen.Page.OnOpenedFromSearch(openWith);
	int framesAskedFor = 0;

	for (int frameIndex = 0; frameIndex < kFramesTheLightLasts + 5; ++frameIndex)
	{
		screen.Frames.DrawPage(kNarrowWidth);

		if (Core::Redraw::TakeDeadline().has_value())
		{
			++framesAskedFor;
		}
	}

	screen.Frames.DrawPage(kNarrowWidth);

	CHECK(framesAskedFor >= kFramesTheLightLasts - 2);
	CHECK(framesAskedFor <= kFramesTheLightLasts);
	CHECK_FALSE(Core::Redraw::TakeDeadline().has_value());
}

TEST_CASE("Opening the page with words that name no colour asks for no frames")
{
	AppearanceScreen screen;
	static_cast<void>(Core::Redraw::TakeDeadline());

	screen.Page.OnOpenedFromSearch("background");
	int framesAskedFor = 0;

	for (int frameIndex = 0; frameIndex < 5; ++frameIndex)
	{
		screen.Frames.DrawPage(kNarrowWidth);

		if (Core::Redraw::TakeDeadline().has_value())
		{
			++framesAskedFor;
		}
	}

	CHECK(framesAskedFor == 0);
}

namespace {

	struct DrawnControl
	{
		float Width = 0.0f;
		float Top = 0.0f;
		float LastItemWidth = 0.0f;
		float LastItemTop = 0.0f;
		float FollowerTop = 0.0f;
		float SwatchWidth = 0.0f;
	};

	DrawnControl DrawControlMeasuredBeforeAFollowerButton(AppearanceScreen& screen, const Graphite::SettingsRow& row)
	{
		DrawnControl drawn;

		screen.Frames.GetFrames().Next([&row, &drawn] {
			ImGui::Begin("Found");
			ImGui::PushID(row.Key.c_str());
			const ImVec2 start = ImGui::GetCursorScreenPos();
			ImGui::BeginGroup();
			row.DrawControl();
			drawn.Width = ImGui::GetItemRectMax().x - start.x;
			drawn.Top = start.y;
			drawn.LastItemWidth = ImGui::GetItemRectSize().x;
			drawn.LastItemTop = ImGui::GetItemRectMin().y;
			drawn.SwatchWidth = ImGui::GetFrameHeight();
			ImGui::SameLine();
			ImGui::Button("follower");
			drawn.FollowerTop = ImGui::GetItemRectMin().y;
			ImGui::EndGroup();
			ImGui::PopID();
			ImGui::End();
		});

		return drawn;
	}
}

TEST_CASE("A found colour's swatch ends its control whether overridden or inherited so the swatches line up beside what follows")
{
	AppearanceScreen screen(true);

	const std::optional<Graphite::SettingsRow> block = screen.Frames.FindRow("Syntax colours");
	REQUIRE(block.has_value());
	const std::vector<Graphite::SettingsRow> overridden = FindInFrame(screen, *block, "comment");
	const std::vector<Graphite::SettingsRow> inherited = FindInFrame(screen, *block, "keyword");
	REQUIRE(overridden.size() == 1);
	REQUIRE_FALSE(inherited.empty());
	REQUIRE(inherited[0].DimmedTitle);

	for (const Graphite::SettingsRow& row : {overridden[0], inherited[0]})
	{
		const DrawnControl drawn = DrawControlMeasuredBeforeAFollowerButton(screen, row);

		CHECK(drawn.Width == doctest::Approx(row.ControlWidth));
		CHECK(drawn.LastItemWidth == doctest::Approx(drawn.SwatchWidth));
		CHECK(drawn.LastItemTop == doctest::Approx(drawn.Top));
		CHECK(drawn.FollowerTop == doctest::Approx(drawn.Top));
	}
}

TEST_CASE("A found colour row's control is as wide as the swatch and wider for an overridden colour's reset icon")
{
	AppearanceScreen builtIn;
	AppearanceScreen own(true);

	const std::optional<Graphite::SettingsRow> builtInBlock = builtIn.Frames.FindRow("Syntax colours");
	const std::optional<Graphite::SettingsRow> ownBlock = own.Frames.FindRow("Syntax colours");
	REQUIRE(builtInBlock.has_value());
	REQUIRE(ownBlock.has_value());

	const std::vector<Graphite::SettingsRow> builtInRows = FindInFrame(builtIn, *builtInBlock, "comment");
	const std::vector<Graphite::SettingsRow> ownOverridden = FindInFrame(own, *ownBlock, "comment");
	const std::vector<Graphite::SettingsRow> ownInherited = FindInFrame(own, *ownBlock, "keyword");

	REQUIRE(builtInRows.size() == 1);
	REQUIRE(ownOverridden.size() == 1);
	REQUIRE_FALSE(ownInherited.empty());
	CHECK(builtInRows[0].ControlWidth > 0.0f);
	CHECK(ownOverridden[0].ControlWidth > builtInRows[0].ControlWidth);
	CHECK(ownInherited[0].ControlWidth == doctest::Approx(builtInRows[0].ControlWidth));
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

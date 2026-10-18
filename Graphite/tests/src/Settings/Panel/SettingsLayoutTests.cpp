#include <doctest/doctest.h>

#include "DrawnColours.h"
#include "ImGuiFrames.h"

#include "Settings/Panel/SettingsLayout.h"
#include "UI/Lists/ListRow.h"

#include "Core/ImGui/Animation.h"
#include "Core/ImGui/TextInput.h"
#include "Core/ImGui/Toggle.h"

#include <imgui.h>

#include <string>
#include <utility>
#include <vector>

namespace {

	const Graphite::CardMeasures kWideCard{.CardWidth = 800.0f, .Padding = 14.0f, .Gap = 16.0f, .StackBelowWidth = 420.0f};
	const Graphite::CardMeasures kNarrowCard{.CardWidth = 400.0f, .Padding = 14.0f, .Gap = 16.0f, .StackBelowWidth = 420.0f};

	constexpr float kWindowHeightThatNeverScrolls = 2000.0f;
	constexpr bool kCompactControl = false;
	constexpr bool kWideControl = true;
	constexpr bool kNotSelected = false;
	constexpr bool kNotDimmed = false;

	ImVec2 CentreOfLastItem()
	{
		const ImVec2 itemMin = ImGui::GetItemRectMin();
		const ImVec2 itemMax = ImGui::GetItemRectMax();
		return ImVec2((itemMin.x + itemMax.x) * 0.5f, (itemMin.y + itemMax.y) * 0.5f);
	}

	struct SampleControls
	{
		bool WordWrap = false;
		std::string Path;
		ImVec2 WordWrapMin;
		float ScrollMaxY = 0.0f;
	};

	void DrawWordWrap(SampleControls& controls)
	{
		Core::Toggle("##wordWrap", &controls.WordWrap);
		controls.WordWrapMin = ImGui::GetItemRectMin();
	}

	void DrawPath(SampleControls& controls)
	{
		Core::InputTextWithHint("##path", "", controls.Path);
	}

	void DrawTable()
	{
		ImGui::TextUnformatted("a table");
	}

	std::vector<Graphite::SettingsCard> SampleCards(SampleControls& controls)
	{
		Graphite::SettingsCard rows{.Heading = "Rows", .Note = "A note under the heading that is long enough to wrap in a narrow panel."};
		rows.Rows.push_back(Graphite::SettingsRow{.Title = "Word wrap",
												  .Description = "Long lines continue on the next line instead of running off to the side.",
												  .ControlWidth = Core::kToggleWidth,
												  .DrawControl = [&controls] { DrawWordWrap(controls); }});
		rows.Rows.push_back(Graphite::SettingsRow{.Title = "No folder is trusted yet."});
		rows.Rows.push_back(Graphite::SettingsRow{.Title = "Requires", .Problem = ".NET 10 runtime \xE2\x80\x94 not found"});
		rows.Rows.push_back(Graphite::SettingsRow{.Title = "Custom path",
												  .Description = "Empty: Graphite's install, then PATH.",
												  .WideControl = true,
												  .DrawControl = [&controls] { DrawPath(controls); }});

		Graphite::SettingsCard blocks{.Heading = "Blocks"};
		blocks.Rows.push_back(Graphite::SettingsRow{.Title = "Prices", .DrawBlock = DrawTable});
		blocks.Rows.push_back(Graphite::SettingsRow{.Title = "Shortcuts", .DrawBlock = DrawTable});

		return {rows, blocks};
	}

	void DrawCardsInWindow(SampleControls& controls, float width)
	{
		Core::Animation::BeginFrame();
		ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
		ImGui::SetNextWindowSize(ImVec2(width, kWindowHeightThatNeverScrolls));
		ImGui::Begin("Settings");

		const std::vector<Graphite::SettingsCard> cards = SampleCards(controls);

		for (std::size_t cardIndex = 0; cardIndex < cards.size(); ++cardIndex)
		{
			ImGui::PushID(static_cast<int>(cardIndex));
			Graphite::SettingsLayout::DrawCard(cards[cardIndex], IM_COL32(255, 80, 80, 255));
			ImGui::PopID();
		}

		controls.ScrollMaxY = ImGui::GetScrollMaxY();
		ImGui::End();
	}

	struct ComboWidths
	{
		float ShortOptions = 0.0f;
		float LongOptions = 0.0f;
		float ExpectedLongOptions = 0.0f;
	};

	void MeasureComboWidths(ComboWidths& widths, const std::vector<std::string>& shortOptions, const std::vector<std::string>& longOptions)
	{
		widths.ShortOptions = Graphite::SettingsLayout::ComboWidth(shortOptions);
		widths.LongOptions = Graphite::SettingsLayout::ComboWidth(longOptions);
		widths.ExpectedLongOptions =
			ImGui::CalcTextSize(longOptions.front().c_str()).x + ImGui::GetStyle().FramePadding.x * 2.0f + ImGui::GetFrameHeight();
	}

	struct ButtonWidths
	{
		float Measured = 0.0f;
		float Expected = 0.0f;
	};

	void MeasureButtonWidths(ButtonWidths& widths, const std::vector<std::string>& labels)
	{
		const ImGuiStyle& style = ImGui::GetStyle();
		widths.Measured = Graphite::SettingsLayout::ButtonsWidth(labels);
		widths.Expected = ImGui::CalcTextSize("Install").x + ImGui::CalcTextSize("Cancel").x + style.FramePadding.x * 4.0f + style.ItemSpacing.x;
	}

	struct SidebarScreen
	{
		std::string Query;
		bool Edited = false;
		bool EntryClicked = false;
		ImVec2 LastItemCentre;
		ImGuiFrames Frames;

		void BeginSidebar()
		{
			ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
			ImGui::SetNextWindowSize(ImVec2(220.0f, 200.0f));
			ImGui::Begin("Sidebar");
		}

		void DrawField()
		{
			BeginSidebar();
			Edited = Graphite::SettingsLayout::DrawSearchField(Query) || Edited;
			LastItemCentre = CentreOfLastItem();
			ImGui::End();
		}

		void DrawFieldFocused()
		{
			BeginSidebar();
			ImGui::SetKeyboardFocusHere();
			Graphite::SettingsLayout::DrawSearchField(Query);
			ImGui::End();
		}

		void DrawEntryFocused()
		{
			BeginSidebar();
			ImGui::SetKeyboardFocusHere();
			EntryClicked = Graphite::SettingsLayout::DrawNavigationEntry("", "Editor", kNotSelected, kNotDimmed) || EntryClicked;
			ImGui::End();
		}

		void DrawEntry()
		{
			BeginSidebar();
			const bool selected = false;
			const bool dimmed = false;
			EntryClicked = Graphite::SettingsLayout::DrawNavigationEntry("", "Editor", selected, dimmed) || EntryClicked;
			LastItemCentre = CentreOfLastItem();
			ImGui::End();
		}
	};
}

TEST_CASE("A row's control stands at the right of a wide card")
{
	const Graphite::RowColumns columns = Graphite::LayOutRowColumns(kWideCard, 100.0f, kCompactControl);

	CHECK_FALSE(columns.Stacked);
	CHECK(columns.ControlWidth == doctest::Approx(100.0f));
	CHECK(columns.ControlX == doctest::Approx(672.0f));
	CHECK(columns.TextWidth == doctest::Approx(656.0f));
}

TEST_CASE("A compact control is never wider than half the card")
{
	const Graphite::RowColumns columns = Graphite::LayOutRowColumns(kWideCard, 500.0f, kCompactControl);

	CHECK(columns.ControlWidth == doctest::Approx(386.0f));
	CHECK(columns.ControlX == doctest::Approx(386.0f));
	CHECK(columns.TextWidth == doctest::Approx(370.0f));
}

TEST_CASE("A wide control takes half the card")
{
	const Graphite::RowColumns columns = Graphite::LayOutRowColumns(kWideCard, 0.0f, kWideControl);

	CHECK(columns.ControlWidth == doctest::Approx(386.0f));
	CHECK_FALSE(columns.Stacked);
}

TEST_CASE("A card narrower than the threshold puts the control under the text")
{
	const Graphite::RowColumns compact = Graphite::LayOutRowColumns(kNarrowCard, 100.0f, kCompactControl);
	const Graphite::RowColumns wide = Graphite::LayOutRowColumns(kNarrowCard, 0.0f, kWideControl);

	CHECK(compact.Stacked);
	CHECK(compact.TextWidth == doctest::Approx(372.0f));
	CHECK(compact.ControlX == doctest::Approx(0.0f));
	CHECK(compact.ControlWidth == doctest::Approx(100.0f));
	CHECK(wide.Stacked);
	CHECK(wide.ControlWidth == doctest::Approx(372.0f));
}

TEST_CASE("A row without a control gives its text the whole inside of the card")
{
	const Graphite::RowColumns wideCard = Graphite::LayOutRowColumns(kWideCard, 0.0f, kCompactControl);
	const Graphite::RowColumns narrowCard = Graphite::LayOutRowColumns(kNarrowCard, 0.0f, kCompactControl);

	CHECK_FALSE(wideCard.Stacked);
	CHECK(wideCard.TextWidth == doctest::Approx(772.0f));
	CHECK_FALSE(narrowCard.Stacked);
}

TEST_CASE("Beside taller text a control is centred on it")
{
	const Graphite::RowColumns columns = Graphite::LayOutRowColumns(kWideCard, 100.0f, kCompactControl);

	const Graphite::RowHeights heights =
		Graphite::MeasureRowHeights(columns, Graphite::RowContent{.TextHeight = 40.0f, .ControlHeight = 23.0f, .Padding = 10.0f, .Spacing = 5.0f});

	CHECK(heights.Row == doctest::Approx(60.0f));
	CHECK(heights.TextTop == doctest::Approx(10.0f));
	CHECK(heights.ControlTop == doctest::Approx(18.5f));
}

TEST_CASE("Beside a taller control the text is centred on it")
{
	const Graphite::RowColumns columns = Graphite::LayOutRowColumns(kWideCard, 100.0f, kCompactControl);

	const Graphite::RowHeights heights =
		Graphite::MeasureRowHeights(columns, Graphite::RowContent{.TextHeight = 17.0f, .ControlHeight = 23.0f, .Padding = 10.0f, .Spacing = 5.0f});

	CHECK(heights.Row == doctest::Approx(43.0f));
	CHECK(heights.TextTop == doctest::Approx(13.0f));
	CHECK(heights.ControlTop == doctest::Approx(10.0f));
}

TEST_CASE("A stacked control sits one spacing under the text")
{
	const Graphite::RowColumns columns = Graphite::LayOutRowColumns(kNarrowCard, 100.0f, kCompactControl);

	const Graphite::RowHeights heights =
		Graphite::MeasureRowHeights(columns, Graphite::RowContent{.TextHeight = 40.0f, .ControlHeight = 23.0f, .Padding = 10.0f, .Spacing = 5.0f});

	CHECK(heights.TextTop == doctest::Approx(10.0f));
	CHECK(heights.ControlTop == doctest::Approx(55.0f));
	CHECK(heights.Row == doctest::Approx(88.0f));
}

TEST_CASE("A combo is as wide as its longest option and never narrower than its minimum")
{
	ImGuiFrames frames;
	ComboWidths widths;
	const std::vector<std::string> shortOptions{"Spaces", "Tabs"};
	const std::vector<std::string> longOptions{"A rather long option that is wider than the minimum"};

	frames.Next([&widths, &shortOptions, &longOptions] { MeasureComboWidths(widths, shortOptions, longOptions); });

	CHECK(widths.ShortOptions == doctest::Approx(140.0f));
	CHECK(widths.LongOptions == doctest::Approx(widths.ExpectedLongOptions));
}

TEST_CASE("A row of buttons is as wide as its buttons and the spacing between them")
{
	ImGuiFrames frames;
	ButtonWidths widths;
	const std::vector<std::string> labels{"Install", "Cancel"};

	frames.Next([&widths, &labels] { MeasureButtonWidths(widths, labels); });

	CHECK(widths.Measured == doctest::Approx(widths.Expected));
}

TEST_CASE("Cards draw every kind of row, wide and narrow, without an ImGui assertion")
{
	ImGuiFrames frames;
	SampleControls controls;

	for (const float width : {900.0f, 500.0f, 300.0f})
	{
		frames.Next([&controls, width] { DrawCardsInWindow(controls, width); });
		frames.Next([&controls, width] { DrawCardsInWindow(controls, width); });
	}

	CHECK(controls.WordWrapMin.x > 0.0f);
}

namespace {

	struct BlockDraws
	{
		int Plain = 0;
		int DrawnEvenOutOfSight = 0;
	};

	void DrawBlocksBelowShortWindow(BlockDraws& draws)
	{
		ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
		ImGui::SetNextWindowSize(ImVec2(600.0f, 200.0f));
		ImGui::Begin("Short");
		ImGui::Dummy(ImVec2(0.0f, 1000.0f));

		Graphite::SettingsCard blocks{.Heading = "Blocks"};
		blocks.Rows.push_back(Graphite::SettingsRow{.Title = "Plain", .DrawBlock = [&draws] {
														++draws.Plain;
														DrawTable();
													}});
		blocks.Rows.push_back(Graphite::SettingsRow{.Title = "Kept",
													.DrawBlock = [&draws] {
														++draws.DrawnEvenOutOfSight;
														DrawTable();
													},
													.DrawnEvenOutOfSight = true});
		Graphite::SettingsLayout::DrawCard(blocks, IM_COL32(255, 80, 80, 255));

		ImGui::End();
	}
}

TEST_CASE("Out of sight, a block is drawn only when it asks to be")
{
	ImGuiFrames frames;
	BlockDraws draws;

	for (int frameIndex = 0; frameIndex < 4; ++frameIndex)
	{
		frames.Next([&draws] { DrawBlocksBelowShortWindow(draws); });
	}

	draws = BlockDraws{};
	frames.Next([&draws] { DrawBlocksBelowShortWindow(draws); });
	frames.Next([&draws] { DrawBlocksBelowShortWindow(draws); });

	CHECK(draws.Plain == 0);
	CHECK(draws.DrawnEvenOutOfSight == 2);
}

TEST_CASE("Cards that fit the window leave nothing to scroll")
{
	ImGuiFrames frames;
	SampleControls controls;

	frames.Next([&controls] { DrawCardsInWindow(controls, 900.0f); });
	frames.Next([&controls] { DrawCardsInWindow(controls, 900.0f); });

	CHECK(controls.ScrollMaxY == doctest::Approx(0.0f));
}

TEST_CASE("In a wide card the control stands at the card's right edge")
{
	ImGuiFrames frames;
	SampleControls controls;

	frames.Next([&controls] { DrawCardsInWindow(controls, 900.0f); });
	frames.Next([&controls] { DrawCardsInWindow(controls, 900.0f); });

	const float windowPadding = 8.0f;
	const float cardInsidePadding = 14.0f;
	const float cardContentWidth = 884.0f;
	CHECK(controls.WordWrapMin.x == doctest::Approx(windowPadding + cardContentWidth - cardInsidePadding - Core::kToggleWidth));
}

TEST_CASE("In a narrow card the control stands under the text at the card's left")
{
	ImGuiFrames frames;
	SampleControls controls;

	frames.Next([&controls] { DrawCardsInWindow(controls, 300.0f); });
	frames.Next([&controls] { DrawCardsInWindow(controls, 300.0f); });

	CHECK(controls.WordWrapMin.x == doctest::Approx(8.0f + 14.0f));
}

TEST_CASE("The search field's clear button empties the query")
{
	SidebarScreen screen;
	screen.Query = "wrap";
	screen.Frames.Next([&screen] { screen.DrawField(); });

	screen.Frames.Click(screen.LastItemCentre, [&screen] { screen.DrawField(); });

	CHECK(screen.Query.empty());
	CHECK(screen.Edited);
}

TEST_CASE("Escape in the search field clears the query")
{
	SidebarScreen screen;
	screen.Query = "wrap";
	screen.Frames.Next([&screen] { screen.DrawFieldFocused(); });
	screen.Frames.Next([&screen] { screen.DrawField(); });
	screen.Frames.Next([&screen] { screen.DrawField(); });

	screen.Frames.PressKey(ImGuiKey_Escape, [&screen] { screen.DrawField(); });

	CHECK(screen.Query.empty());
}

TEST_CASE("A navigation entry reports a click")
{
	SidebarScreen screen;
	screen.Frames.Next([&screen] { screen.DrawEntry(); });

	screen.Frames.Click(screen.LastItemCentre, [&screen] { screen.DrawEntry(); });

	CHECK(screen.EntryClicked);
}

namespace {

	constexpr float kNavigationEntryHeight = 28.0f;
	constexpr int kFramesPastTheHoverFade = 30;

	// An entry alone in a sidebar without a resize grip, which ImGui draws in the accent at a fifth, as a selected
	// row is. Every test names its entry apart: the hover's fade is kept under the entry's id from one test to the
	// next.
	struct NavigationEntryScreen
	{
		ImGuiFrames Frames;
		std::string Title;
		bool Selected = false;
		float Top = 0.0f;
		float Bottom = 0.0f;

		explicit NavigationEntryScreen(std::string title) : Title(std::move(title)) {}

		void Draw()
		{
			Core::Animation::BeginFrame();
			ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
			ImGui::SetNextWindowSize(ImVec2(220.0f, 200.0f));
			ImGui::Begin("Sidebar", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);
			Graphite::SettingsLayout::DrawNavigationEntry("", Title, Selected, kNotDimmed);
			Top = ImGui::GetItemRectMin().y;
			Bottom = ImGui::GetItemRectMax().y;
			ImGui::End();
		}

		void DrawFrames(int count)
		{
			for (int frame = 0; frame < count; ++frame)
			{
				Frames.Next([this] { Draw(); });
			}
		}

		void PointAtTheEntry()
		{
			const ImGuiWindow* sidebar = ImGui::FindWindowByName("Sidebar");
			REQUIRE(sidebar != nullptr);
			ImGui::GetIO().AddMousePosEvent(sidebar->InnerRect.Min.x + 40.0f, (Top + Bottom) * 0.5f);
		}

		static bool DrawsIn(const ImVec4& colour)
		{
			const ImGuiWindow* sidebar = ImGui::FindWindowByName("Sidebar");
			REQUIRE(sidebar != nullptr);
			return DrawsInColour(*sidebar->DrawList, ImGui::GetColorU32(colour));
		}

		static bool DrawsIn(ImGuiCol colour) { return DrawsIn(ImGui::GetStyleColorVec4(colour)); }
	};
}

TEST_CASE("The settings navigation keeps its height but uses the list colours")
{
	NavigationEntryScreen screen("Colours of a list row");
	screen.Selected = true;
	screen.DrawFrames(1);

	CHECK(screen.Bottom - screen.Top == doctest::Approx(kNavigationEntryHeight));
	CHECK(NavigationEntryScreen::DrawsIn(Graphite::CurrentListRowColours().Selected));
	CHECK_FALSE(NavigationEntryScreen::DrawsIn(ImGuiCol_Header));

	screen.Selected = false;
	screen.PointAtTheEntry();
	screen.DrawFrames(kFramesPastTheHoverFade);

	CHECK(NavigationEntryScreen::DrawsIn(Graphite::CurrentListRowColours().Hovered));
	CHECK_FALSE(NavigationEntryScreen::DrawsIn(ImGuiCol_HeaderHovered));
	CHECK_FALSE(NavigationEntryScreen::DrawsIn(Graphite::CurrentListRowColours().Selected));
}

TEST_CASE("A selected navigation entry under the pointer keeps the accent")
{
	NavigationEntryScreen screen("Selected under the pointer");
	screen.Selected = true;
	screen.DrawFrames(1);

	screen.PointAtTheEntry();
	screen.DrawFrames(kFramesPastTheHoverFade);

	CHECK(NavigationEntryScreen::DrawsIn(Graphite::CurrentListRowColours().Selected));
	CHECK_FALSE(NavigationEntryScreen::DrawsIn(Graphite::CurrentListRowColours().Hovered));
}

TEST_CASE("A navigation entry held down under the pointer is the accent at three tenths")
{
	NavigationEntryScreen screen("Held down");
	screen.DrawFrames(1);

	screen.PointAtTheEntry();
	screen.DrawFrames(kFramesPastTheHoverFade);
	ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, true);
	screen.DrawFrames(1);

	CHECK(NavigationEntryScreen::DrawsIn(Graphite::CurrentListRowColours().Pressed));
	CHECK_FALSE(NavigationEntryScreen::DrawsIn(ImGuiCol_HeaderActive));
}

TEST_CASE("A search result's page label is followed by a gap before its first card")
{
	ImGuiFrames frames;
	ImVec2 lastItemSize;

	frames.Next([&lastItemSize] {
		ImGui::Begin("Results");
		Graphite::SettingsLayout::DrawResultsLabel("Key Bindings");
		lastItemSize = ImGui::GetItemRectSize();
		ImGui::End();
	});

	CHECK(lastItemSize.x == 0.0f);
	CHECK(lastItemSize.y > 0.0f);
}

TEST_CASE("A focused navigation entry is clicked by Space")
{
	SidebarScreen screen;
	ImGui::GetIO().ConfigNavCursorVisibleAlways = true;
	screen.Frames.Next([&screen] { screen.DrawEntryFocused(); });
	screen.Frames.Next([&screen] { screen.DrawEntry(); });

	screen.Frames.PressKey(ImGuiKey_Space, [&screen] { screen.DrawEntry(); });

	CHECK(screen.EntryClicked);
}

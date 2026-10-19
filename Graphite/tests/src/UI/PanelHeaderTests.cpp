#include <doctest/doctest.h>

#include "UI/PanelHeader.h"

#include "DrawnColours.h"
#include "HoveredItems.h"
#include "ImGuiFrames.h"

#include "Core/ImGui/Animation.h"
#include "Core/ImGui/IconButton.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <IconsMaterialDesignIcons.h>

#include <cmath>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace {
	constexpr ImVec2 kWideWindow{400.0f, 300.0f};
	constexpr ImVec2 kNarrowWindow{200.0f, 300.0f};
	constexpr float kOnePixel = 1.0f;
	constexpr float kAboveTheRow = 2.0f;
	constexpr float kIntoTheDropdown = 10.0f;
	constexpr int kFramesPastTheHoverFade = 30;
	constexpr const char* kWindowName = "Header";

	struct StackDepths
	{
		int Colours = 0;
		int StyleVars = 0;
		int Fonts = 0;
		int Ids = 0;

		bool operator==(const StackDepths&) const = default;
	};

	StackDepths CurrentStackDepths()
	{
		const ImGuiContext& context = *ImGui::GetCurrentContext();
		return StackDepths{.Colours = context.ColorStack.Size,
						   .StyleVars = context.StyleVarStack.Size,
						   .Fonts = context.FontStack.Size,
						   .Ids = context.CurrentWindow->IDStack.Size};
	}

	struct HeaderScreen
	{
		ImGuiFrames Frames;
		ImVec2 WindowSize = kWideWindow;
		std::vector<Graphite::PanelHeaderChoice> Choices;
		std::vector<Graphite::PanelHeaderAction> Actions;
		std::vector<Graphite::PanelHeaderResult> Reported;
		ImVec2 RowStart;
		ImVec2 CursorAfter;
		float ContentRight = 0.0f;
		bool StacksKept = true;

		void Draw()
		{
			Core::Animation::BeginFrame();

			ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
			ImGui::SetNextWindowSize(WindowSize);
			ImGui::Begin(kWindowName, nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);

			RowStart = ImGui::GetCursorScreenPos();
			ContentRight = RowStart.x + ImGui::GetContentRegionAvail().x;

			const StackDepths before = CurrentStackDepths();
			const Graphite::PanelHeaderResult result = Graphite::DrawPanelHeader(Choices, Actions);
			StacksKept = StacksKept && CurrentStackDepths() == before;

			CursorAfter = ImGui::GetCursorScreenPos();
			ImGui::Dummy(ImVec2(kOnePixel, kOnePixel));

			ImGui::End();

			const bool reportsSomething = result.Chosen[0].has_value() || result.Chosen[1].has_value() ||
										  result.ClickedAction.has_value();

			if (reportsSomething)
			{
				Reported.push_back(result);
			}
		}

		void Next()
		{
			Frames.Next([this] { Draw(); });
		}

		void Click(ImVec2 at)
		{
			Frames.Click(at, [this] { Draw(); });
		}

		void MoveTo(ImVec2 at)
		{
			ImGui::GetIO().AddMousePosEvent(at.x, at.y);
			Next();
		}

		float RowMiddle() const { return RowStart.y + Graphite::PanelHeaderHeight() * 0.5f; }

		std::vector<HoveredStretch> ItemsAcrossTheRow()
		{
			return HoveredAcross(Frames, RowMiddle(), RowStart.x, ContentRight, [this] { Draw(); });
		}

		HoveredStretch ItemDownTheRowAt(float x)
		{
			const std::vector<HoveredStretch> stretches =
				HoveredDown(Frames, x, RowStart.y - kAboveTheRow, RowStart.y + Graphite::PanelHeaderHeight() + kAboveTheRow,
							[this] { Draw(); });
			REQUIRE(stretches.size() == 1);
			return stretches.front();
		}

		void ChooseOption(int index)
		{
			REQUIRE(Frames.ClickPopupRow(index, [this] { Draw(); }));
		}

		static const ImDrawList& Drawing()
		{
			const ImGuiWindow* window = ImGui::FindWindowByName(kWindowName);
			REQUIRE(window != nullptr);
			return *window->DrawList;
		}
	};

	Graphite::PanelHeaderChoice ModeChoice(const char* id, int selected = 0)
	{
		return Graphite::PanelHeaderChoice{
			.Id = id, .Options = {"File System", "Visual Studio Solution"}, .Selected = selected};
	}

	bool StandsInTheMiddleOf(const HoveredStretch& item, float rowTop, float rowHeight)
	{
		const float above = item.From - rowTop;
		const float below = rowTop + rowHeight - (item.To + kOnePixel);
		return above > 0.0f && below > 0.0f && std::abs(above - below) <= kOnePixel;
	}
}

TEST_CASE("The header is one row of 34 px")
{
	HeaderScreen screen;
	screen.Choices = {ModeChoice("##rowHeightMode")};
	screen.Actions = {{.Id = "##rowHeightRefresh", .Glyph = ICON_MDI_REFRESH, .Tooltip = "Refresh"}};
	screen.Next();

	CHECK(Graphite::PanelHeaderHeight() == doctest::Approx(Graphite::kPanelHeaderHeight));
	CHECK(screen.CursorAfter.x == doctest::Approx(screen.RowStart.x));
	CHECK(screen.CursorAfter.y ==
		  doctest::Approx(screen.RowStart.y + Graphite::kPanelHeaderHeight + ImGui::GetStyle().ItemSpacing.y));
}

TEST_CASE("The header's dropdown and its buttons stand in the middle of its row")
{
	HeaderScreen screen;
	screen.Choices = {ModeChoice("##middleMode")};
	screen.Actions = {{.Id = "##middleRefresh", .Glyph = ICON_MDI_REFRESH, .Tooltip = "Refresh"}};
	screen.Next();

	const HoveredStretch dropdown = screen.ItemDownTheRowAt(screen.RowStart.x + kIntoTheDropdown);
	const HoveredStretch button = screen.ItemDownTheRowAt(screen.ContentRight - Core::IconButtonSize() * 0.5f);

	CHECK(StandsInTheMiddleOf(dropdown, screen.RowStart.y, Graphite::kPanelHeaderHeight));
	CHECK(StandsInTheMiddleOf(button, screen.RowStart.y, Graphite::kPanelHeaderHeight));
}

TEST_CASE("The header's dropdowns stand at its left and its buttons at its right")
{
	HeaderScreen screen;
	screen.Choices = {ModeChoice("##endsMode")};
	screen.Actions = {{.Id = "##endsNewFile", .Glyph = ICON_MDI_FILE_PLUS_OUTLINE, .Tooltip = "New File"},
					  {.Id = "##endsRefresh", .Glyph = ICON_MDI_REFRESH, .Tooltip = "Refresh"}};
	screen.Next();

	const std::vector<HoveredStretch> items = screen.ItemsAcrossTheRow();

	REQUIRE(items.size() == 3);
	CHECK(items[0].From == doctest::Approx(screen.RowStart.x));
	CHECK(items[2].To + kOnePixel == doctest::Approx(screen.ContentRight));
	CHECK(items[1].From > items[0].To);
}

TEST_CASE("A dropdown is as wide as the name of its current option")
{
	HeaderScreen screen;
	screen.Choices = {ModeChoice("##widthMode", 0)};
	screen.Next();
	const std::vector<HoveredStretch> fileSystem = screen.ItemsAcrossTheRow();

	screen.Choices = {ModeChoice("##widthMode", 1)};
	screen.Next();
	const std::vector<HoveredStretch> solution = screen.ItemsAcrossTheRow();

	REQUIRE(fileSystem.size() == 1);
	REQUIRE(solution.size() == 1);

	const float grownBy = (solution[0].To - solution[0].From) - (fileSystem[0].To - fileSystem[0].From);
	const float namesDifferBy =
		ImGui::CalcTextSize("Visual Studio Solution").x - ImGui::CalcTextSize("File System").x;
	CHECK(std::abs(grownBy - namesDifferBy) <= kOnePixel + kOnePixel);
}

TEST_CASE("Choosing a dropdown option reports it")
{
	HeaderScreen screen;
	screen.Choices = {ModeChoice("##chooseMode")};
	screen.Next();

	screen.Click(ImVec2(screen.RowStart.x + kIntoTheDropdown, screen.RowMiddle()));

	REQUIRE(ImGui::GetCurrentContext()->OpenPopupStack.Size == 1);
	CHECK(screen.Reported.empty());

	screen.ChooseOption(1);

	REQUIRE(screen.Reported.size() == 1);
	CHECK(screen.Reported.front().Chosen[0] == std::optional<int>{1});
	CHECK_FALSE(screen.Reported.front().Chosen[1].has_value());
	CHECK_FALSE(screen.Reported.front().ClickedAction.has_value());
	CHECK(ImGui::GetCurrentContext()->OpenPopupStack.Size == 0);
}

TEST_CASE("An action button reports its index")
{
	HeaderScreen screen;
	screen.Actions = {{.Id = "##indexNewFile", .Glyph = ICON_MDI_FILE_PLUS_OUTLINE, .Tooltip = "New File"},
					  {.Id = "##indexNewFolder", .Glyph = ICON_MDI_FOLDER_PLUS_OUTLINE, .Tooltip = "New Folder"},
					  {.Id = "##indexRefresh", .Glyph = ICON_MDI_REFRESH, .Tooltip = "Refresh"}};
	screen.Next();

	const std::vector<HoveredStretch> buttons = screen.ItemsAcrossTheRow();
	REQUIRE(buttons.size() == 3);

	screen.Click(ImVec2(buttons[1].Middle(), screen.RowMiddle()));

	REQUIRE(screen.Reported.size() == 1);
	CHECK(screen.Reported.front().ClickedAction == std::optional<std::size_t>{1});
	CHECK_FALSE(screen.Reported.front().Chosen[0].has_value());
}

TEST_CASE("Two choices sit side by side")
{
	HeaderScreen screen;
	screen.Choices = {ModeChoice("##pairMode", 1),
					  Graphite::PanelHeaderChoice{.Id = "##pairSolution", .Options = {"First.slnx", "Second.slnx"}, .Selected = 0}};
	screen.Next();

	const std::vector<HoveredStretch> dropdowns = screen.ItemsAcrossTheRow();
	REQUIRE(dropdowns.size() == 2);
	CHECK(dropdowns[1].From > dropdowns[0].To);

	const HoveredStretch first = screen.ItemDownTheRowAt(dropdowns[0].Middle());
	const HoveredStretch second = screen.ItemDownTheRowAt(dropdowns[1].Middle());
	CHECK(first.From == doctest::Approx(second.From));
	CHECK(first.To == doctest::Approx(second.To));

	screen.Click(ImVec2(dropdowns[1].Middle(), screen.RowMiddle()));
	screen.ChooseOption(1);

	REQUIRE(screen.Reported.size() == 1);
	CHECK_FALSE(screen.Reported.front().Chosen[0].has_value());
	CHECK(screen.Reported.front().Chosen[1] == std::optional<int>{1});
}

TEST_CASE("In a narrow header the dropdowns stop short of the buttons")
{
	HeaderScreen screen;
	screen.WindowSize = kNarrowWindow;
	screen.Choices = {ModeChoice("##narrowMode", 1),
					  Graphite::PanelHeaderChoice{
						  .Id = "##narrowSolution", .Options = {"AVeryLongSolutionName.slnx"}, .Selected = 0}};
	screen.Actions = {{.Id = "##narrowNewFile", .Glyph = ICON_MDI_FILE_PLUS_OUTLINE, .Tooltip = "New File"},
					  {.Id = "##narrowNewFolder", .Glyph = ICON_MDI_FOLDER_PLUS_OUTLINE, .Tooltip = "New Folder"},
					  {.Id = "##narrowRefresh", .Glyph = ICON_MDI_REFRESH, .Tooltip = "Refresh"}};
	screen.Next();

	const std::vector<HoveredStretch> items = screen.ItemsAcrossTheRow();

	REQUIRE(items.size() == 5);

	for (std::size_t index = 1; index < items.size(); ++index)
	{
		CHECK(items[index].From > items[index - 1].To);
	}

	CHECK(items[4].To <= screen.ContentRight);
}

TEST_CASE("An open dropdown keeps its hover fill while its popup is open")
{
	HeaderScreen screen;
	screen.Choices = {ModeChoice("##openFillMode")};
	const ImVec2 awayFromTheHeader(kIntoTheDropdown + kIntoTheDropdown, kWideWindow.y - kIntoTheDropdown);
	const ImU32 hoverFill = ImGui::GetColorU32(ImGuiCol_ButtonHovered);

	screen.MoveTo(awayFromTheHeader);
	CHECK_FALSE(DrawsInColour(HeaderScreen::Drawing(), hoverFill));

	screen.Click(ImVec2(screen.RowStart.x + kIntoTheDropdown, screen.RowMiddle()));
	REQUIRE(ImGui::GetCurrentContext()->OpenPopupStack.Size == 1);

	for (int frame = 0; frame < kFramesPastTheHoverFade; ++frame)
	{
		screen.MoveTo(awayFromTheHeader);
	}

	REQUIRE(ImGui::GetCurrentContext()->OpenPopupStack.Size == 1);
	CHECK(DrawsInColour(HeaderScreen::Drawing(), hoverFill));
}

TEST_CASE("The header leaves ImGui's colour, style, font and id stacks as it found them, with a popup open or not")
{
	HeaderScreen screen;
	screen.Choices = {ModeChoice("##balanceMode", 1),
					  Graphite::PanelHeaderChoice{.Id = "##balanceSolution", .Options = {"One.slnx", "Two.slnx"}, .Selected = 1}};
	screen.Actions = {{.Id = "##balanceRefresh", .Glyph = ICON_MDI_REFRESH, .Tooltip = "Refresh"}};
	screen.Next();

	screen.Click(ImVec2(screen.RowStart.x + kIntoTheDropdown, screen.RowMiddle()));
	REQUIRE(ImGui::GetCurrentContext()->OpenPopupStack.Size == 1);
	screen.Next();

	CHECK(screen.StacksKept);
}

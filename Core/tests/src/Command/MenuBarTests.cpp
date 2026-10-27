#include <doctest/doctest.h>

#include "Core/Command/Command.h"
#include "Core/Command/CommandRegistry.h"
#include "Core/Command/Keymap.h"
#include "Core/Command/MenuBar.h"
#include "Core/Command/MenuItem.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cfloat>
#include <optional>
#include <vector>

namespace {
	constexpr float kFontSize = 16.0f;
	constexpr float kRowHeight = 28.0f;
	constexpr float kItemHeight = 22.0f;
	constexpr ImVec2 kWindowSize(600.0f, 300.0f);
	constexpr const char* kWindowName = "##menuBarTest";
	constexpr const char* kMenuLabel = "File";
	constexpr float kIntoTheLabel = 2.0f;
	constexpr float kHalf = 0.5f;

	struct Extent
	{
		float Left = FLT_MAX;
		float Right = -FLT_MAX;
		float Top = FLT_MAX;
		float Bottom = -FLT_MAX;
	};

	std::optional<Extent> ExtentInColour(const ImDrawList& drawing, ImU32 colour)
	{
		Extent extent;
		bool found = false;

		for (const ImDrawVert& vertex : drawing.VtxBuffer)
		{
			if (vertex.col != colour)
			{
				continue;
			}

			found = true;
			extent.Left = std::min(extent.Left, vertex.pos.x);
			extent.Right = std::max(extent.Right, vertex.pos.x);
			extent.Top = std::min(extent.Top, vertex.pos.y);
			extent.Bottom = std::max(extent.Bottom, vertex.pos.y);
		}

		if (!found)
		{
			return std::nullopt;
		}

		return extent;
	}

	class MenuBarWindow
	{
	public:
		MenuBarWindow()
		{
			m_Context = ImGui::CreateContext();

			ImGuiIO& inputOutput = ImGui::GetIO();
			inputOutput.DisplaySize = ImVec2(800.0f, 600.0f);
			inputOutput.DeltaTime = 1.0f / 60.0f;
			inputOutput.IniFilename = nullptr;
			inputOutput.Fonts->Build();
			ImGui::GetStyle().FontSizeBase = kFontSize;

			m_Commands.Add(Core::Command{.Id = "test.open", .Label = "Open", .Enabled = [this] {
											 m_SpacingInTheDropdown = ImGui::GetStyle().ItemSpacing;
											 return true;
										 }});
			m_Commands.Add(Core::Command{.Id = "test.busy",
										 .Label = "Busy",
										 .Run = [this] { ++m_BusyRuns; },
										 .Enabled = [this] {
											 m_BusyItemTop = ImGui::GetCursorScreenPos();
											 return false;
										 },
										 .DisabledNote = "Waiting for something else",
										 .RunWhileDisabled = [this] { ++m_BusyOffersTaken; }});
			m_Tree.push_back(Core::MenuItem::Submenu(
				kMenuLabel, {Core::MenuItem::Command("test.open"), Core::MenuItem::Command("test.busy")}));
		}

		~MenuBarWindow() { ImGui::DestroyContext(m_Context); }

		MenuBarWindow(const MenuBarWindow&) = delete;
		MenuBarWindow& operator=(const MenuBarWindow&) = delete;

		void Draw(const Core::MenuBarHeights& heights)
		{
			ImGui::NewFrame();
			ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
			ImGui::SetNextWindowSize(kWindowSize);

			// As the dock space host does: a window's menu bar takes its height from the frame padding while Begin runs.
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
								ImVec2(ImGui::GetStyle().FramePadding.x, (heights.Row - ImGui::GetFontSize()) * kHalf));
			ImGui::Begin(kWindowName, nullptr,
						 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
							 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_MenuBar);
			ImGui::PopStyleVar();

			m_SpacingInTheBar = ImGui::GetStyle().ItemSpacing;
			m_Bar.Render(m_Commands, m_Keymap, m_Tree, heights, nullptr, nullptr);
			m_LabelWidth = ImGui::CalcTextSize(kMenuLabel).x;
			m_HoverFill = ImGui::GetColorU32(ImGuiCol_HeaderHovered);

			ImGui::End();
			ImGui::EndFrame();
		}

		void HoverTheMenu(const Core::MenuBarHeights& heights)
		{
			Draw(heights);
			ImGui::GetIO().AddMousePosEvent(LabelLeft() + kIntoTheLabel, heights.Row * kHalf);
			Draw(heights);
			Draw(heights);
		}

		void OpenTheMenu(const Core::MenuBarHeights& heights)
		{
			HoverTheMenu(heights);
			ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, true);
			Draw(heights);
			ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, false);
			Draw(heights);
			Draw(heights);
		}

		static float LabelLeft()
		{
			const ImGuiStyle& style = ImGui::GetStyle();

			return std::max(style.WindowPadding.x, style.ItemSpacing.x) + style.ItemSpacing.x * kHalf;
		}

		static const ImDrawList& Drawing()
		{
			const ImGuiWindow* window = ImGui::FindWindowByName(kWindowName);
			REQUIRE(window != nullptr);
			return *window->DrawList;
		}

		void ClickTheBusyItem(const Core::MenuBarHeights& heights)
		{
			REQUIRE(m_BusyItemTop.has_value());
			const ImVec2 intoTheItem(m_BusyItemTop->x + kIntoTheLabel * 4.0f, m_BusyItemTop->y + ImGui::GetFontSize() * kHalf);

			ImGui::GetIO().AddMousePosEvent(intoTheItem.x, intoTheItem.y);
			Draw(heights);
			ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, true);
			Draw(heights);
			ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, false);
			Draw(heights);
			Draw(heights);
		}

		static bool IsTheMenuOpen() { return !ImGui::GetCurrentContext()->OpenPopupStack.empty(); }

		ImVec2 GetSpacingInTheBar() const { return m_SpacingInTheBar; }
		std::optional<ImVec2> GetSpacingInTheDropdown() const { return m_SpacingInTheDropdown; }
		float GetLabelWidth() const { return m_LabelWidth; }
		ImU32 GetHoverFill() const { return m_HoverFill; }
		int GetBusyRuns() const { return m_BusyRuns; }
		int GetBusyOffersTaken() const { return m_BusyOffersTaken; }

	private:
		ImGuiContext* m_Context = nullptr;
		Core::CommandRegistry m_Commands;
		Core::Keymap m_Keymap;
		std::vector<Core::MenuItem> m_Tree;
		Core::MenuBar m_Bar;

		ImVec2 m_SpacingInTheBar;
		std::optional<ImVec2> m_SpacingInTheDropdown;
		float m_LabelWidth = 0.0f;
		ImU32 m_HoverFill = 0;
		std::optional<ImVec2> m_BusyItemTop;
		int m_BusyRuns = 0;
		int m_BusyOffersTaken = 0;
	};
}

TEST_CASE("A top-level menu fills the given item height under the pointer, centred in the bar's row")
{
	MenuBarWindow window;
	const Core::MenuBarHeights heights{.Row = kRowHeight, .Item = kItemHeight};

	window.HoverTheMenu(heights);

	const std::optional<Extent> fill = ExtentInColour(MenuBarWindow::Drawing(), window.GetHoverFill());
	REQUIRE(fill.has_value());

	CHECK(fill->Bottom - fill->Top == doctest::Approx(kItemHeight));
	CHECK(fill->Top == doctest::Approx((kRowHeight - kItemHeight) * kHalf));
}

TEST_CASE("A top-level menu's fill reaches the item spacing past its label on each side")
{
	MenuBarWindow window;
	const Core::MenuBarHeights heights{.Row = kRowHeight, .Item = kItemHeight};

	window.HoverTheMenu(heights);

	const std::optional<Extent> fill = ExtentInColour(MenuBarWindow::Drawing(), window.GetHoverFill());
	REQUIRE(fill.has_value());

	const float spacing = window.GetSpacingInTheBar().x;
	CHECK(fill->Left == doctest::Approx(MenuBarWindow::LabelLeft() - spacing));
	CHECK(fill->Right - fill->Left == doctest::Approx(window.GetLabelWidth() + spacing * 2.0f));
}

TEST_CASE("A dropdown keeps the item spacing the bar had before its items were sized")
{
	MenuBarWindow window;
	const Core::MenuBarHeights heights{.Row = kRowHeight, .Item = kItemHeight};

	window.OpenTheMenu(heights);

	REQUIRE(window.GetSpacingInTheDropdown().has_value());
	CHECK(window.GetSpacingInTheDropdown()->x == doctest::Approx(window.GetSpacingInTheBar().x));
	CHECK(window.GetSpacingInTheDropdown()->y == doctest::Approx(window.GetSpacingInTheBar().y));
}

TEST_CASE("A disabled command that offers something runs its offer on a click and closes the menu, never Run")
{
	MenuBarWindow window;
	const Core::MenuBarHeights heights{.Row = kRowHeight, .Item = kItemHeight};

	window.OpenTheMenu(heights);
	REQUIRE(MenuBarWindow::IsTheMenuOpen());

	window.ClickTheBusyItem(heights);

	CHECK(window.GetBusyOffersTaken() == 1);
	CHECK(window.GetBusyRuns() == 0);
	CHECK_FALSE(MenuBarWindow::IsTheMenuOpen());
}

TEST_CASE("With no item height a top-level menu keeps ImGui's fill, a line and the item spacing tall")
{
	MenuBarWindow window;
	const Core::MenuBarHeights heights{.Row = kRowHeight};

	window.HoverTheMenu(heights);

	const std::optional<Extent> fill = ExtentInColour(MenuBarWindow::Drawing(), window.GetHoverFill());
	REQUIRE(fill.has_value());

	CHECK(fill->Bottom - fill->Top == doctest::Approx(kFontSize + window.GetSpacingInTheBar().y));
}

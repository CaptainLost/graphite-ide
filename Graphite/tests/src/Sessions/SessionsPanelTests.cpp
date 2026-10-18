#include <doctest/doctest.h>

#include "DrawnColours.h"
#include "ImGuiFrames.h"

#include "Sessions/SessionsPanel.h"
#include "UI/Lists/ListRow.h"

#include "Core/ImGui/FontSet.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <optional>

using namespace Graphite;

namespace {

	constexpr float kFringe = 1.5f;
	constexpr float kHighlightInset = 6.0f;
	constexpr float kAwayFromTheRight = 20.0f;
	constexpr int kRowCount = 2;
	constexpr int kFirstRow = 0;
	constexpr int kSecondRow = 1;

	struct Extent
	{
		float Left = FLT_MAX;
		float Right = -FLT_MAX;
		float Top = FLT_MAX;
		float Bottom = -FLT_MAX;
	};

	std::optional<Extent> ExtentInColour(const ImDrawList& drawing, ImU32 colour, float fromY = -FLT_MAX,
										 float toY = FLT_MAX)
	{
		std::optional<Extent> extent;

		for (const ImDrawVert& vertex : drawing.VtxBuffer)
		{
			if (vertex.col != colour || vertex.pos.y < fromY || vertex.pos.y > toY)
			{
				continue;
			}

			if (!extent.has_value())
			{
				extent = Extent{};
			}

			extent->Left = std::min(extent->Left, vertex.pos.x);
			extent->Right = std::max(extent->Right, vertex.pos.x);
			extent->Top = std::min(extent->Top, vertex.pos.y);
			extent->Bottom = std::max(extent->Bottom, vertex.pos.y);
		}

		return extent;
	}

	bool WithinFringe(float drawn, float expected)
	{
		return std::abs(drawn - expected) <= kFringe;
	}

	// The sessions list as the panel holds it: a child window with no padding of its own, one row per session.
	struct SessionListScreen
	{
		ImGuiFrames Frames;
		ImRect List;
		float RowTops[kRowCount] = {};
		float Pitch = 0.0f;

		SessionListScreen() { ImGui::GetStyle().FontSizeBase = Core::kInterfaceFontSize; }

		void Draw()
		{
			ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
			ImGui::SetNextWindowSize(ImVec2(320.0f, 240.0f));
			ImGui::Begin("Panel", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);
			ImGui::BeginChild("##sessionList");

			List = ImGui::GetCurrentWindowRead()->InnerRect;
			Pitch = ListRowPitch();

			for (int row = 0; row < kRowCount; ++row)
			{
				ImGui::PushID(row);
				RowTops[row] = ImGui::GetCursorScreenPos().y;
				DrawSessionRow(SessionRowText{.Title = "A session", .Detail = "$0.10  5 min"});
				ImGui::PopID();
			}

			ImGui::EndChild();
			ImGui::End();
		}

		void DrawFrame()
		{
			Frames.Next([this] { Draw(); });
		}

		static const ImDrawList& DrawList()
		{
			const ImGuiWindow* panel = ImGui::FindWindowByName("Panel");
			REQUIRE(panel != nullptr);
			REQUIRE(!panel->DC.ChildWindows.empty());
			return *panel->DC.ChildWindows.back()->DrawList;
		}
	};
}

TEST_CASE("The sessions list draws through ListRowSelectable: its row pitch equals ListRowPitch()")
{
	SessionListScreen screen;
	screen.DrawFrame();

	ImGui::GetIO().AddMousePosEvent(screen.List.Max.x - kAwayFromTheRight, screen.RowTops[kSecondRow] + screen.Pitch * 0.5f);
	screen.DrawFrame();

	const ImU32 hoveredColour = ImGui::GetColorU32(CurrentListRowColours().Hovered);
	const std::optional<Extent> hovered = ExtentInColour(SessionListScreen::DrawList(), hoveredColour);
	REQUIRE(hovered.has_value());

	CHECK(WithinFringe(hovered->Top, screen.RowTops[kSecondRow]));
	CHECK(WithinFringe(hovered->Bottom - hovered->Top, screen.Pitch));
	CHECK(WithinFringe(hovered->Left, screen.List.Min.x + kHighlightInset));
	CHECK(WithinFringe(hovered->Right, screen.List.Max.x - kHighlightInset));
	CHECK_FALSE(DrawsInColour(SessionListScreen::DrawList(), ImGui::GetColorU32(ImGuiCol_HeaderHovered)));

	const std::optional<Extent> title = ExtentInColour(SessionListScreen::DrawList(), ImGui::GetColorU32(ImGuiCol_Text),
													   hovered->Top, hovered->Bottom);
	REQUIRE(title.has_value());
	CHECK(title->Left > hovered->Left);

	const std::optional<Extent> details = ExtentInColour(SessionListScreen::DrawList(), ImGui::GetColorU32(ImGuiCol_TextDisabled),
														 screen.RowTops[kFirstRow], screen.RowTops[kSecondRow]);
	REQUIRE(details.has_value());
	CHECK(details->Top >= screen.RowTops[kFirstRow] + screen.Pitch);
}

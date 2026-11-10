#include "Sessions/Overview/DashboardView.h"

#include "Sessions/Overview/ChartTiles.h"
#include "Sessions/Overview/DashboardCanvas.h"
#include "Sessions/Overview/ListTiles.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <array>

namespace Graphite {

	namespace {
		constexpr std::array<NumberTile, kNumberTileCount> kNumberTiles = {
			NumberTile::Spent,		NumberTile::ThisWeek,	NumberTile::PerActiveDay, NumberTile::Sessions,
			NumberTile::Subagents, NumberTile::CacheSaved, NumberTile::Output,		  NumberTile::BusiestDay};

		float AreaHeight(DashboardArea area, float width)
		{
			switch (area)
			{
			case DashboardArea::Map:
				return MapHeight(width);
			case DashboardArea::Daily:
				return DailyHeight(width);
			case DashboardArea::Models:
				return ModelsHeight(width);
			case DashboardArea::Projects:
				return ListTileHeight(kTopRows + 1);
			case DashboardArea::Sessions:
				return ListTileHeight(kTopRows);
			case DashboardArea::Tools:
			case DashboardArea::Skills:
				return ListTileHeight(kListRows);
			case DashboardArea::Punch:
				return PunchHeight(width);
			case DashboardArea::Cache:
				return CacheHeight(width);
			case DashboardArea::Count:
				break;
			}

			return 0.0f;
		}

		void DrawArea(DashboardCanvas& canvas, DashboardArea area, const ImRect& tile)
		{
			switch (area)
			{
			case DashboardArea::Map:
				DrawMap(canvas, tile);
				return;
			case DashboardArea::Daily:
				DrawDaily(canvas, tile);
				return;
			case DashboardArea::Models:
				DrawModels(canvas, tile);
				return;
			case DashboardArea::Projects:
				DrawProjects(canvas, tile);
				return;
			case DashboardArea::Sessions:
				DrawSessions(canvas, tile);
				return;
			case DashboardArea::Tools:
				DrawTools(canvas, tile);
				return;
			case DashboardArea::Skills:
				DrawSkills(canvas, tile);
				return;
			case DashboardArea::Punch:
				DrawPunch(canvas, tile);
				return;
			case DashboardArea::Cache:
				DrawCache(canvas, tile);
				return;
			case DashboardArea::Count:
				return;
			}
		}

		ImRect Moved(const ImRect& rect, ImVec2 by)
		{
			return ImRect(ImVec2(rect.Min.x + by.x, rect.Min.y + by.y), ImVec2(rect.Max.x + by.x, rect.Max.y + by.y));
		}

		// WARNING: Each tile ends with its Dummy, so the window's last call is an item and ImGui's cursor check holds.
		template <typename Draw>
		void DrawTile(DashboardCanvas& canvas, const ImRect& tile, Draw draw)
		{
			canvas.Drawing.PushClipRect(tile.Min, tile.Max, true);
			draw(tile);
			canvas.Drawing.PopClipRect();

			ImGui::SetCursorScreenPos(tile.Min);
			ImGui::Dummy(tile.GetSize());
		}

		bool IsEmpty(const TipCard& card)
		{
			return card.Head.empty() && card.HeadValue.empty() && card.Banner.empty() && card.Body.empty() &&
				   card.Notes.empty() && card.Sections.empty();
		}

		void ShowHovered(const DashboardRegion& hovered, const DashboardColours& colours, DashboardAction& action)
		{
			if (hovered.ShowDay || hovered.OpenSession)
			{
				ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
			}

			if (hovered.Tip)
			{
				const TipCard card = hovered.Tip();

				if (!IsEmpty(card) && BeginTipCard(colours))
				{
					DrawTipCard(card, colours);
					ImGui::EndTooltip();
				}
			}

			const ImGuiIO& io = ImGui::GetIO();
			if (ImGui::IsMouseReleased(ImGuiMouseButton_Left) && hovered.Rect.Contains(io.MouseClickedPos[ImGuiMouseButton_Left]))
			{
				action.ShowDay = hovered.ShowDay;
				action.OpenSession = hovered.OpenSession;
			}
		}
	}

	DashboardGrid LayOutDashboard(float width)
	{
		const float unit = ImGui::GetFontSize();
		return PlaceTiles(width, unit, NumberTileHeight(unit), AreaHeight);
	}

	DashboardAction DrawDashboard(const DashboardModel& model, const LocalCalendar& calendar, const DashboardColours& colours)
	{
		DashboardAction action;

		if (ImGui::BeginChild("##dashboard"))
		{
			const ImVec2 origin = ImGui::GetCursorScreenPos();
			const DashboardGrid grid = LayOutDashboard(ImGui::GetContentRegionAvail().x);
			DashboardCanvas canvas{.Model = model,
								   .Calendar = calendar,
								   .Colours = colours,
								   .Drawing = *ImGui::GetWindowDrawList(),
								   .Unit = ImGui::GetFontSize(),
								   .PointerInside = ImGui::IsWindowHovered()};

			for (std::size_t index = 0; index < kNumberTileCount; ++index)
			{
				DrawTile(canvas, Moved(grid.Numbers[index], origin),
						 [&](const ImRect& tile) { DrawNumberTile(canvas, tile, kNumberTiles[index]); });
			}

			for (std::size_t index = 0; index < kDashboardAreaCount; ++index)
			{
				const auto area = static_cast<DashboardArea>(index);
				DrawTile(canvas, Moved(grid.Areas[index], origin), [&](const ImRect& tile) { DrawArea(canvas, area, tile); });
			}

			if (const DashboardRegion* hovered = canvas.Hovered())
			{
				ShowHovered(*hovered, colours, action);
			}
		}

		ImGui::EndChild();
		return action;
	}
}

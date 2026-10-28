#include "Image/PictureCanvas.h"

#include "Core/ImGui/ImageTexture.h"
#include "Core/ImGui/UiScale.h"

#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace Graphite {

	namespace {
		constexpr ImU32 kGridColour = IM_COL32(0, 0, 0, 128);
		constexpr float kGridLineWidth = 1.0f;
		constexpr float kCheckerSide = 8.0f;
		constexpr float kSmallestItemSide = 1.0f;

		// A touchpad reports a fraction of a notch at a time; the fractions add up and a step comes with each whole notch.
		int TakeWholeNotches(float& pending, float wheel)
		{
			pending += wheel;
			const int notches = static_cast<int>(std::trunc(pending));
			pending -= static_cast<float>(notches);
			return notches;
		}

		float SteppedZoom(float zoom, int notches)
		{
			for (int step = 0; step < std::abs(notches); ++step)
			{
				zoom = notches > 0 ? StepIn(zoom) : StepOut(zoom);
			}

			return zoom;
		}

		void HandleInput(ZoomState& zoom, ImVec2 picture, const ImRect& area, bool hovered, bool dragging)
		{
			const ImGuiIO& io = ImGui::GetIO();

			if (dragging)
			{
				zoom.Pan.x += io.MouseDelta.x;
				zoom.Pan.y += io.MouseDelta.y;
				zoom = ClampPan(zoom, picture, area, PanKeepVisible());
			}

			if (!hovered)
			{
				zoom.WheelPending = 0.0f;
				return;
			}

			// WARNING: The wheel is claimed on every hovered frame, not only when a notch arrives. ImGui applies the
			// wheel to the window's own scroll at the start of the next frame and tests the ownership set during this one.
			ImGui::SetKeyOwner(ImGuiKey_MouseWheelY, ImGui::GetItemID());

			if (const int notches = TakeWholeNotches(zoom.WheelPending, io.MouseWheel); notches != 0)
			{
				zoom = ZoomAround(zoom, SteppedZoom(zoom.Zoom, notches), io.MousePos, picture, area, PanKeepVisible());
			}

			if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
			{
				zoom.Zoom = zoom.Zoom == 1.0f ? FitZoom(picture, area.GetSize(), FitMargin()) : 1.0f;
				zoom.Pan = ImVec2(0.0f, 0.0f);
			}
		}

		// Squares fixed in screen pixels from the picture's top-left corner, drawn only where the picture is seen.
		void DrawCheckerboard(ImDrawList& drawList, const ImRect& placed, const ImRect& visible)
		{
			const CheckerboardColours colours = ThemeCheckerboardColours();
			const float side = Core::Scaled(kCheckerSide);
			const auto edge = [side](float start, int square) { return start + static_cast<float>(square) * side; };

			drawList.AddRectFilled(visible.Min, visible.Max, colours.Near);

			const int firstColumn = static_cast<int>(std::floor((visible.Min.x - placed.Min.x) / side));
			const int firstRow = static_cast<int>(std::floor((visible.Min.y - placed.Min.y) / side));

			for (int row = firstRow; edge(placed.Min.y, row) < visible.Max.y; ++row)
			{
				const float top = edge(placed.Min.y, row);
				const int firstFarColumn = firstColumn + ((firstColumn + row) % 2 == 0 ? 1 : 0);

				for (int column = firstFarColumn; edge(placed.Min.x, column) < visible.Max.x; column += 2)
				{
					const float left = edge(placed.Min.x, column);
					ImRect square(left, top, left + side, top + side);
					square.ClipWithFull(visible);
					drawList.AddRectFilled(square.Min, square.Max, colours.Far);
				}
			}
		}

		void DrawBackground(ImDrawList& drawList, PictureBackground background, const ImRect& placed,
							const ImRect& visible)
		{
			switch (background)
			{
			case PictureBackground::Checkerboard:
				DrawCheckerboard(drawList, placed, visible);
				return;
			case PictureBackground::Dark:
				drawList.AddRectFilled(visible.Min, visible.Max, kDarkPictureBackground);
				return;
			case PictureBackground::Light:
				drawList.AddRectFilled(visible.Min, visible.Max, kLightPictureBackground);
				return;
			}
		}

		struct LineSpan
		{
			int First = 0;
			int Last = -1;
		};

		// The lines between file pixels on one axis that fall within [from, to]; line n starts file pixel n.
		LineSpan LinesWithin(float start, float pitch, int pixels, float from, float to)
		{
			return {std::max(1, static_cast<int>(std::ceil((from - start) / pitch))),
					std::min(pixels - 1, static_cast<int>(std::floor((to - start) / pitch)))};
		}

		void DrawPixelGrid(ImDrawList& drawList, ImVec2 picture, const ImRect& placed, const ImRect& visible)
		{
			const float across = placed.GetWidth() / picture.x;
			const float down = placed.GetHeight() / picture.y;

			const LineSpan columns =
				LinesWithin(placed.Min.x, across, static_cast<int>(picture.x), visible.Min.x, visible.Max.x);

			for (int column = columns.First; column <= columns.Last; ++column)
			{
				const float x = std::round(placed.Min.x + static_cast<float>(column) * across);
				drawList.AddRectFilled(ImVec2(x, visible.Min.y), ImVec2(x + kGridLineWidth, visible.Max.y), kGridColour);
			}

			const LineSpan rows = LinesWithin(placed.Min.y, down, static_cast<int>(picture.y), visible.Min.y, visible.Max.y);

			for (int row = rows.First; row <= rows.Last; ++row)
			{
				const float y = std::round(placed.Min.y + static_cast<float>(row) * down);
				drawList.AddRectFilled(ImVec2(visible.Min.x, y), ImVec2(visible.Max.x, y + kGridLineWidth), kGridColour);
			}
		}

		void DrawPicture(const ZoomState& zoom, const PictureCanvasInput& input, const ImRect& area)
		{
			const ImRect placed = PlacePicture(zoom, input.Picture, area);
			ImRect visible = placed;
			visible.ClipWithFull(area);

			if (visible.GetWidth() <= 0.0f || visible.GetHeight() <= 0.0f)
			{
				return;
			}

			ImDrawList& drawList = *ImGui::GetWindowDrawList();
			drawList.PushClipRect(area.Min, area.Max, true);

			DrawBackground(drawList, input.Background, placed, visible);

			if (input.Texture != nullptr)
			{
				const Core::TextureSampling sampling =
					zoom.Zoom >= kNearestFromZoom ? Core::TextureSampling::Nearest : Core::TextureSampling::Smooth;
				Core::DrawTexture(drawList, *input.Texture, placed, sampling);
			}

			if (input.PixelGrid && zoom.Zoom >= kPixelGridFromZoom)
			{
				DrawPixelGrid(drawList, input.Picture, placed, visible);
			}

			drawList.PopClipRect();
		}
	}

	PictureCanvasResult DrawPictureCanvas(const char* id, ZoomState& zoom, const PictureCanvasInput& input)
	{
		ImGuiWindow* const window = ImGui::GetCurrentWindow();
		const ImVec2 cursor = ImGui::GetCursorScreenPos();
		const ImVec2 available = ImGui::GetContentRegionAvail();

		// The canvas covers the window's inner rectangle from the cursor down, as the path row spans it across.
		const ImVec2 origin(window->InnerRect.Min.x, cursor.y);
		const ImVec2 room(window->InnerRect.Max.x - origin.x, window->InnerRect.Max.y - origin.y);
		const bool hasRoom = room.x > 0.0f && room.y > 0.0f;

		// WARNING: The item is laid out at the size of the content region and takes the whole inner rectangle only as
		// the rectangle it answers to. Laid out that wide it would reach past the window's padding, and the window would
		// grow scrollbars. A region with no room left is still one pixel, as a zero size is refused.
		const ImVec2 layout(std::max(available.x, kSmallestItemSide), std::max(available.y, kSmallestItemSide));
		const ImVec2 size(std::max(room.x, kSmallestItemSide), std::max(room.y, kSmallestItemSide));
		const ImRect area(origin, ImVec2(origin.x + size.x, origin.y + size.y));
		const ImGuiID itemId = window->GetID(id);

		ImGui::ItemSize(layout);

		if (!ImGui::ItemAdd(area, itemId))
		{
			PictureCanvasResult hidden;
			hidden.Area = area;
			return hidden;
		}

		ImGui::ButtonBehavior(area, itemId, nullptr, nullptr);

		PictureCanvasResult result;
		result.Area = area;

		// Opening in a region with no room would fit the picture to a pixel and keep it that small.
		if (!zoom.Opened)
		{
			if (!hasRoom)
			{
				return result;
			}

			zoom.Zoom = OpeningZoom(input.Picture, area.GetSize(), FitMargin());
			zoom.Pan = ImVec2(0.0f, 0.0f);
			zoom.Opened = true;
		}

		const bool hovered = ImGui::IsItemHovered();
		const bool dragging = ImGui::IsItemActive();
		HandleInput(zoom, input.Picture, area, hovered, dragging);

		if (hovered || dragging)
		{
			ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
		}

		DrawPicture(zoom, input, area);

		if (hovered)
		{
			result.Hovered = PixelUnder(ImGui::GetIO().MousePos, zoom, input.Picture, area);
		}

		return result;
	}
}

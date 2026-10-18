#include "corepch.h"

#include "Core/ImGui/IconButton.h"

#include "Core/ImGui/Animation.h"
#include "Core/ImGui/ImGuiColors.h"
#include "Core/ImGui/UiScale.h"

#include <imgui_internal.h>

#include <cmath>

namespace Core {

	namespace {
		constexpr float kButtonInset = 4.0f;
		constexpr float kHalf = 0.5f;
		constexpr float kSeparatorHeightPerFontSize = 0.9f;
		constexpr float kInvisibleAlpha = 0.0f;
		constexpr float kNoTextBaseline = -1.0f;

		void DrawFace(const ImRect& rect, const char* glyph, const ImVec4& fill, ImGuiCol glyphColour)
		{
			ImDrawList* drawList = ImGui::GetWindowDrawList();

			if (fill.w > 0.0f)
			{
				drawList->AddRectFilled(rect.Min, rect.Max, ImGui::GetColorU32(fill), ImGui::GetStyle().FrameRounding);
			}

			DrawCentredGlyph(drawList, rect, glyph, ImGui::GetColorU32(glyphColour));
		}
	}

	void DrawCentredGlyph(ImDrawList* drawList, const ImRect& rect, const char* glyph, ImU32 colour)
	{
		// WARNING: floored, not rounded: CalcTextSize rounds up, and a glyph centred onto a half pixel
		// is resampled and reads blurrier than the text beside it.
		const ImVec2 glyphSize = ImGui::CalcTextSize(glyph);
		const ImVec2 glyphPosition(std::floor(rect.Min.x + (rect.GetWidth() - glyphSize.x) * kHalf),
								   std::floor(rect.Min.y + (rect.GetHeight() - glyphSize.y) * kHalf));

		drawList->AddText(glyphPosition, colour, glyph);
	}

	float IconButtonSize(IconButtonFit fit)
	{
		if (fit == IconButtonFit::Line)
		{
			return ImGui::GetTextLineHeight();
		}

		return std::floor(ImGui::GetFontSize() + Scaled(kButtonInset));
	}

	// WARNING: drawn rather than submitted as an ImGui::Button, which put the glyphs off centre: on
	// a square the width of a row, the frame padding leaves an inner box narrower than the glyph,
	// the text no longer fits, and RenderTextClipped falls back to aligning it left.
	bool DrawIconButton(const char* id, const char* glyph, bool engaged, const char* tooltip,
						const IconButtonOptions& options)
	{
		ImGuiWindow* const window = ImGui::GetCurrentWindow();

		if (window->SkipItems)
		{
			return false;
		}

		const float size = IconButtonSize(options.Fit);
		const bool framed = options.Fit == IconButtonFit::Frame;
		const float cellHeight = framed ? ImGui::GetFrameHeight() : size;

		const ImVec2 cellMin = window->DC.CursorPos;
		const float squareTop = cellMin.y + std::floor((cellHeight - size) * kHalf);
		const ImRect rect(ImVec2(cellMin.x, squareTop), ImVec2(cellMin.x + size, squareTop + size));
		const ImGuiID itemId = window->GetID(id);

		// WARNING: The whole cell is laid out and only the square inside it is the item. Moving the cursor down to
		// centre the square would take the line down with it, and whatever follows on the line would start low.
		ImGui::ItemSize(ImVec2(size, cellHeight), framed ? ImGui::GetStyle().FramePadding.y : kNoTextBaseline);

		const bool navigable = (options.Flags & ImGuiButtonFlags_EnableNav) != 0;

		if (!ImGui::ItemAdd(rect, itemId, nullptr, navigable ? ImGuiItemFlags_None : ImGuiItemFlags_NoNav))
		{
			return false;
		}

		bool hovered = false;
		bool pressedDown = false;
		const bool pressed = ImGui::ButtonBehavior(rect, itemId, &hovered, &pressedDown, options.Flags);
		const bool held = pressedDown && hovered;

		const float hoverAmount = Animation::HoverAmount(itemId, hovered);

		const ImVec4 hoveredFill = ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered);
		const ImVec4 restingFill =
			engaged ? ImGui::GetStyleColorVec4(ImGuiCol_Button) : WithAlphaReplaced(hoveredFill, kInvisibleAlpha);
		const ImVec4 fill = held ? ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive)
								 : Animation::Blend(restingFill, hoveredFill, hoverAmount);

		DrawFace(rect, glyph, fill, engaged ? ImGuiCol_CheckMark : ImGuiCol_Text);

		ImGui::SetItemTooltip("%s", tooltip);

		return pressed;
	}

	void DrawIconButtonFace(const ImRect& rect, const char* glyph, bool hovered, bool held)
	{
		const ImVec4 hoveredFill = ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered);
		ImVec4 fill = WithAlphaReplaced(hoveredFill, kInvisibleAlpha);

		if (held)
		{
			fill = ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive);
		}
		else if (hovered)
		{
			fill = hoveredFill;
		}

		DrawFace(rect, glyph, fill, ImGuiCol_Text);
	}

	void DrawIconSeparator()
	{
		const float height = std::floor(ImGui::GetFontSize() * kSeparatorHeightPerFontSize);

		const ImVec2 separatorStart = ImGui::GetCursorScreenPos();
		const float separatorTop = separatorStart.y + std::floor((ImGui::GetFrameHeight() - height) * kHalf);

		ImGui::GetWindowDrawList()->AddLine(ImVec2(separatorStart.x, separatorTop),
											ImVec2(separatorStart.x, separatorTop + height),
											ImGui::GetColorU32(ImGuiCol_Separator), Scaled(kIconSeparatorWidth));

		ImGui::Dummy(ImVec2(Scaled(kIconSeparatorWidth), ImGui::GetFrameHeight()));
	}
}

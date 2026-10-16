#include "UI/Chevron.h"

#include "Core/ImGui/ImGuiColors.h"

#include <IconsMaterialDesignIcons.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cmath>

namespace Graphite {

	namespace {
		constexpr float kHalf = 0.5f;
		constexpr float kClearAlpha = 0.0f;
		constexpr float kPlainPaddingsBeforeLabel = 2.0f;
		constexpr float kFramedPaddingsBeforeLabel = 3.0f;
	}

	const char* ChevronGlyph(bool open)
	{
		return open ? ICON_MDI_CHEVRON_DOWN : ICON_MDI_CHEVRON_RIGHT;
	}

	void DrawChevron(bool open)
	{
		ImGui::TextDisabled("%s", ChevronGlyph(open));
	}

	float ChevronWidth()
	{
		return std::max(ImGui::CalcTextSize(ICON_MDI_CHEVRON_RIGHT).x, ImGui::CalcTextSize(ICON_MDI_CHEVRON_DOWN).x);
	}

	// WARNING: ImGui draws its arrow in the text colour and has no flag to leave it out, so the text colour is
	// cleared for the call. The label shares that colour, so it is drawn again here, in the row's own colour and
	// where TreeNodeBehavior puts it: the offsets below are its own, for a framed node and a plain one.
	bool ChevronTreeNode(const char* id, ImGuiTreeNodeFlags flags, const char* label)
	{
		// WARNING: TreeNodeEx returns before submitting anything in a window that skips its items, and what this
		// would ask of the last item would be asked of another one.
		if (ImGui::GetCurrentWindowRead()->SkipItems)
		{
			return false;
		}

		const ImGuiStyle& style = ImGui::GetStyle();
		const float fontSize = ImGui::GetFontSize();
		const bool framed = (flags & ImGuiTreeNodeFlags_Framed) != 0;
		const bool framePadded = framed || (flags & ImGuiTreeNodeFlags_FramePadding) != 0;

		const float lineTextOffset = ImGui::GetCurrentWindow()->DC.CurrLineTextBaseOffset;
		const float paddingsBeforeLabel = framed ? kFramedPaddingsBeforeLabel : kPlainPaddingsBeforeLabel;
		const float textOffsetY = framePadded ? std::max(style.FramePadding.y, lineTextOffset) : lineTextOffset;

		const bool opens = (flags & ImGuiTreeNodeFlags_Leaf) == 0;
		const ImVec2 rowStart = ImGui::GetCursorScreenPos();
		ImVec2 textTopLeft(rowStart.x + fontSize + style.FramePadding.x * paddingsBeforeLabel, rowStart.y + textOffsetY);

		// A framed leaf has no arrow and ImGui moves its label to the start; a plain leaf keeps the arrow's room.
		if (framed && !opens)
		{
			textTopLeft.x = rowStart.x + style.FramePadding.x;
		}

		ImGui::PushStyleColor(ImGuiCol_Text, Core::WithAlphaReplaced(ImGui::GetStyleColorVec4(ImGuiCol_Text), kClearAlpha));
		const bool open = ImGui::TreeNodeEx(id, flags, "%s", label);
		ImGui::PopStyleColor();

		if (!ImGui::IsItemVisible())
		{
			return open;
		}

		if (opens)
		{
			// WARNING: Floored, because CalcTextSize rounds up and a glyph centred onto a half pixel reads blurrier
			// than the text beside it.
			const char* const glyph = ChevronGlyph(open);
			const float arrowLeft = rowStart.x + style.FramePadding.x;
			const float glyphLeft = std::floor(arrowLeft + (fontSize - ImGui::CalcTextSize(glyph).x) * kHalf);
			ImGui::GetWindowDrawList()->AddText(ImVec2(glyphLeft, textTopLeft.y),
												ImGui::GetColorU32(ImGuiCol_TextDisabled), glyph);
		}

		if (*label == '\0')
		{
			return open;
		}

		constexpr const char* kUpToTheTerminator = nullptr;
		constexpr bool kHideTextAfterHashes = false;

		if (framed)
		{
			ImGui::RenderTextClipped(textTopLeft, ImGui::GetItemRectMax(), label, kUpToTheTerminator, nullptr);
		}
		else
		{
			ImGui::RenderText(textTopLeft, label, kUpToTheTerminator, kHideTextAfterHashes);
		}

		return open;
	}
}

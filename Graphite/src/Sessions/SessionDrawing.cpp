#include "Sessions/SessionDrawing.h"

#include "UI/Segment.h"

#include "Core/Base/Base.h"

#include <IconsLucide.h>

namespace Graphite {

	namespace {
		constexpr float kCategoryBarWidth = 3.0f;
		constexpr float kNoSpacing = 0.0f;
	}

	void ColouredText(ImU32 colour, const std::string& text)
	{
		ImGui::PushStyleColor(ImGuiCol_Text, colour);
		ImGui::TextUnformatted(text.c_str(), text.c_str() + text.size());
		ImGui::PopStyleColor();
	}

	void CategoryBar(ImU32 colour, float height)
	{
		const ImVec2 origin = ImGui::GetCursorScreenPos();
		ImGui::GetWindowDrawList()->AddRectFilled(origin, ImVec2(origin.x + kCategoryBarWidth, origin.y + height),
												  colour);
	}

	const char* IconOf(ToolCategory category)
	{
		switch (category)
		{
		case ToolCategory::Read:
			return ICON_LC_FILE_TEXT;
		case ToolCategory::Edit:
			return ICON_LC_PENCIL;
		case ToolCategory::Run:
			return ICON_LC_SQUARE_TERMINAL;
		case ToolCategory::Agent:
			return ICON_LC_BOT;
		case ToolCategory::Web:
			return ICON_LC_GLOBE;
		case ToolCategory::Other:
			return ICON_LC_HAMMER;
		}
		return ICON_LC_HAMMER;
	}

	void DrawPiece(ImVec2& origin, ImU32 colour, const std::string& text)
	{
		ImGui::GetWindowDrawList()->AddText(origin, colour, text.c_str(), text.c_str() + text.size());
		origin.x += ImGui::CalcTextSize(text.c_str(), text.c_str() + text.size()).x;
	}

	bool SegmentButton(const char* label, bool selected)
	{
		return DrawSmallSegment(label, selected) && !selected;
	}

	bool SegmentedChoice(std::span<const char* const> labels, int& selected)
	{
		bool moved = false;

		for (std::size_t optionIndex = 0; optionIndex < labels.size(); ++optionIndex)
		{
			if (optionIndex > 0)
			{
				ImGui::SameLine(kNoSpacing, kNoSpacing);
			}

			const int optionId = static_cast<int>(optionIndex);
			ImGui::PushID(optionId);

			if (SegmentButton(labels[optionIndex], optionId == selected))
			{
				selected = optionId;
				moved = true;
			}

			ImGui::PopID();
		}

		return moved;
	}
}

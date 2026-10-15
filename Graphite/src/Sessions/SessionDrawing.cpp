#include "Sessions/SessionDrawing.h"

#include "Core/Base/Base.h"

#include <IconsMaterialDesignIcons.h>

namespace Graphite {

	namespace {
		constexpr float kCategoryBarWidth = 3.0f;
		constexpr float kNoSpacing = 0.0f;
		constexpr int kSegmentButtonColourCount = 2;
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
			return ICON_MDI_FILE_DOCUMENT_OUTLINE;
		case ToolCategory::Edit:
			return ICON_MDI_PENCIL;
		case ToolCategory::Run:
			return ICON_MDI_CONSOLE;
		case ToolCategory::Agent:
			return ICON_MDI_ROBOT_OUTLINE;
		case ToolCategory::Web:
			return ICON_MDI_WEB;
		case ToolCategory::Other:
			return ICON_MDI_TOOLS;
		}
		return ICON_MDI_TOOLS;
	}

	void DrawPiece(ImVec2& origin, ImU32 colour, const std::string& text)
	{
		ImGui::GetWindowDrawList()->AddText(origin, colour, text.c_str(), text.c_str() + text.size());
		origin.x += ImGui::CalcTextSize(text.c_str(), text.c_str() + text.size()).x;
	}

	bool SegmentButton(const char* label, bool selected)
	{
		const ImVec4* colours = ImGui::GetStyle().Colors;

		ImGui::PushStyleColor(ImGuiCol_Button, selected ? colours[ImGuiCol_ButtonActive] : colours[ImGuiCol_FrameBg]);
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
							  selected ? colours[ImGuiCol_ButtonActive] : colours[ImGuiCol_ButtonHovered]);
		const bool clicked = ImGui::SmallButton(label);
		ImGui::PopStyleColor(kSegmentButtonColourCount);

		return clicked && !selected;
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

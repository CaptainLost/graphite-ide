#include "Sessions/SessionDrawing.h"

#include "Core/Base/Base.h"

#include <IconsMaterialDesignIcons.h>

#include <cfloat>

namespace Graphite {

	void ColouredText(ImU32 colour, const std::string& text)
	{
		ImGui::PushStyleColor(ImGuiCol_Text, colour);
		ImGui::TextUnformatted(text.c_str(), text.c_str() + text.size());
		ImGui::PopStyleColor();
	}

	void CategoryBar(ImU32 colour, float height)
	{
		const ImVec2 at = ImGui::GetCursorScreenPos();
		ImGui::GetWindowDrawList()->AddRectFilled(at, ImVec2(at.x + 3.0f, at.y + height), colour);
	}

	std::string Fitted(const std::string& text, float width)
	{
		ImFont* font = ImGui::GetFont();
		const float size = ImGui::GetFontSize();
		const char* begin = text.c_str();
		const char* end = begin + text.size();

		if (font->CalcTextSizeA(size, FLT_MAX, 0.0f, begin, end).x <= width)
		{
			return text;
		}

		const float ellipsis = font->CalcTextSizeA(size, FLT_MAX, 0.0f, kEllipsis.data(), kEllipsis.data() + kEllipsis.size()).x;
		if (width < ellipsis)
		{
			return {};
		}

		const char* remaining = begin;
		font->CalcTextSizeA(size, width - ellipsis, 0.0f, begin, end, &remaining);

		// Backed off to a lead byte all the same, should the font ever stop mid-character.
		while (remaining > begin && (static_cast<unsigned char>(*remaining) & 0xC0u) == 0x80u)
		{
			--remaining;
		}

		return std::string{begin, remaining}.append(kEllipsis);
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

	void DrawPiece(ImVec2& at, ImU32 colour, const std::string& text)
	{
		ImGui::GetWindowDrawList()->AddText(at, colour, text.c_str(), text.c_str() + text.size());
		at.x += ImGui::CalcTextSize(text.c_str(), text.c_str() + text.size()).x;
	}

	bool SegmentButton(const char* label, bool selected)
	{
		const ImVec4* colours = ImGui::GetStyle().Colors;

		// The chosen option keeps the pressed colour under the mouse too: hovering it offers
		// nothing to press.
		ImGui::PushStyleColor(ImGuiCol_Button, selected ? colours[ImGuiCol_ButtonActive] : colours[ImGuiCol_FrameBg]);
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
							  selected ? colours[ImGuiCol_ButtonActive] : colours[ImGuiCol_ButtonHovered]);
		const bool clicked = ImGui::SmallButton(label);
		ImGui::PopStyleColor(2);

		return clicked && !selected;
	}

	bool SegmentedChoice(std::span<const char* const> labels, int& selected)
	{
		bool moved = false;

		for (std::size_t index = 0; index < labels.size(); ++index)
		{
			if (index > 0)
			{
				ImGui::SameLine(0.0f, 0.0f);
			}

			ImGui::PushID(static_cast<int>(index));

			if (SegmentButton(labels[index], static_cast<int>(index) == selected))
			{
				selected = static_cast<int>(index);
				moved = true;
			}

			ImGui::PopID();
		}

		return moved;
	}
}

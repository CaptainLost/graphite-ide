#include "Git/Work/ChipRow.h"

#include <imgui.h>

namespace Graphite {

	namespace {

		constexpr float kChipPaddingX = 6.0f;
		constexpr float kChipPaddingY = 2.0f;

		void DrawChip(const std::string& text, const ImVec2& size)
		{
			ImGui::Dummy(size);

			const ImVec2 min = ImGui::GetItemRectMin();
			ImDrawList* const drawList = ImGui::GetWindowDrawList();
			drawList->AddRectFilled(min, ImGui::GetItemRectMax(), ImGui::GetColorU32(ImGuiCol_FrameBg),
									ImGui::GetStyle().FrameRounding);
			drawList->AddText(ImVec2(min.x + kChipPaddingX, min.y + kChipPaddingY),
							  ImGui::GetColorU32(ImGuiCol_TextDisabled), text.c_str());
		}
	}

	float ChipHeight()
	{
		return ImGui::GetTextLineHeight() + 2.0f * kChipPaddingY;
	}

	void DrawChipRow(const std::vector<std::string>& chips)
	{
		if (chips.empty())
		{
			ImGui::Dummy(ImVec2(0.0f, ChipHeight()));
			return;
		}

		const float spacing = ImGui::GetStyle().ItemSpacing.x;
		const float right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
		bool first = true;

		for (const std::string& chip : chips)
		{
			const ImVec2 size(ImGui::CalcTextSize(chip.c_str()).x + 2.0f * kChipPaddingX, ChipHeight());

			if (!first)
			{
				const float after = ImGui::GetItemRectMax().x + spacing;

				if (after + size.x <= right)
				{
					ImGui::SameLine();
				}
			}

			DrawChip(chip, size);
			first = false;
		}
	}
}

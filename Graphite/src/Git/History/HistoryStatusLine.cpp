#include "Git/History/HistoryStatusLine.h"

#include "Core/ImGui/LoadingState.h"
#include "Core/ImGui/TextButton.h"

#include <imgui.h>

#include <algorithm>
#include <string>

namespace Graphite {

	namespace {

		constexpr const char* kLoadMore = "Load 500 more";
		constexpr const char* kLoadAll = "Load all";
		constexpr const char* kSearchFurther = "Search further";
		constexpr const char* kSearchAll = "Search all";
		constexpr const char* kFoldThem = "Fold them";
		constexpr std::string_view kBetween = " \xC2\xB7 ";

		constexpr float kPaddingSides = 2.0f;
		constexpr float kNoOffset = 0.0f;
		constexpr float kNoSpacing = 0.0f;

		float SmallButtonWidth(const char* label)
		{
			return ImGui::CalcTextSize(label).x + ImGui::GetStyle().FramePadding.x * kPaddingSides;
		}
	}

	float HistoryStatusLineHeight()
	{
		return ImGui::GetTextLineHeightWithSpacing();
	}

	std::optional<HistoryClick> DrawHistoryStatusLine(const HistoryStatus& status, std::string_view foldFooter)
	{
		std::optional<HistoryClick> clicked;

		const bool busy = !status.Busy.empty();
		const bool saysText = !status.Text.empty();

		// A busy line without text holds the loader alone.
		if (saysText || !busy)
		{
			ImGui::TextDisabled("%s", status.Text.c_str());
		}

		if (!foldFooter.empty())
		{
			const std::string footer = (saysText ? std::string{kBetween} : std::string{}) + std::string{foldFooter};

			if (saysText || !busy)
			{
				ImGui::SameLine(kNoOffset, kNoSpacing);
			}

			ImGui::TextDisabled("%s", footer.c_str());
			ImGui::SameLine(kNoOffset, kNoSpacing);

			ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetColorU32(ImGuiCol_CheckMark));

			if (Core::SmallTextButton(kFoldThem))
			{
				clicked = HistoryClick::Fold;
			}

			ImGui::PopStyleColor();
		}

		const bool drewSomething = saysText || !busy || !foldFooter.empty();

		if (busy)
		{
			if (drewSomething)
			{
				ImGui::SameLine();
			}

			Core::LoadingLine(status.Busy.c_str(), Core::WhileLoaderWaits::ShowText);

			return clicked;
		}

		if (status.Actions == HistoryActions::None)
		{
			return clicked;
		}

		const bool searching = status.Actions == HistoryActions::Search;
		const char* more = searching ? kSearchFurther : kLoadMore;
		const char* all = searching ? kSearchAll : kLoadAll;

		const float buttonsWidth = SmallButtonWidth(more) + ImGui::GetStyle().ItemSpacing.x + SmallButtonWidth(all);

		// WARNING: The buttons keep to the right edge but never cover the text: in a column too narrow
		// for both they follow it on the same line instead.
		ImGui::SameLine();
		const float textEnd = ImGui::GetCursorPosX();
		ImGui::SetCursorPosX(std::max(textEnd, textEnd + ImGui::GetContentRegionAvail().x - buttonsWidth));

		if (Core::SmallTextButton(more))
		{
			clicked = HistoryClick::More;
		}

		ImGui::SameLine();

		if (Core::SmallTextButton(all))
		{
			clicked = HistoryClick::All;
		}

		return clicked;
	}
}

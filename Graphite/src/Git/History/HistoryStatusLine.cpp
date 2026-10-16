#include "Git/History/HistoryStatusLine.h"

#include "Core/ImGui/LoadingState.h"

#include <imgui.h>

#include <algorithm>

namespace Graphite {

	namespace {

		constexpr const char* kLoadMore = "Load 500 more";
		constexpr const char* kLoadAll = "Load all";
		constexpr const char* kSearchFurther = "Search further";
		constexpr const char* kSearchAll = "Search all";

		constexpr float kPaddingSides = 2.0f;

		float SmallButtonWidth(const char* label)
		{
			return ImGui::CalcTextSize(label).x + ImGui::GetStyle().FramePadding.x * kPaddingSides;
		}
	}

	float HistoryStatusLineHeight()
	{
		return ImGui::GetTextLineHeightWithSpacing();
	}

	std::optional<HistoryClick> DrawHistoryStatusLine(const HistoryStatus& status)
	{
		ImGui::TextDisabled("%s", status.Text.c_str());

		if (!status.Busy.empty())
		{
			ImGui::SameLine();

			Core::LoadingLine(status.Busy.c_str(), Core::WhileLoaderWaits::ShowText);

			return std::nullopt;
		}

		if (status.Actions == HistoryActions::None)
		{
			return std::nullopt;
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

		std::optional<HistoryClick> clicked;

		if (ImGui::SmallButton(more))
		{
			clicked = HistoryClick::More;
		}

		ImGui::SameLine();

		if (ImGui::SmallButton(all))
		{
			clicked = HistoryClick::All;
		}

		return clicked;
	}
}

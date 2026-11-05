#include "Git/History/HistoryStatusLine.h"

#include "Core/ImGui/LoadingState.h"
#include "Core/ImGui/TextButton.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
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

		// The mockup's footer, in its pixels at a 13 pixel font.
		constexpr float kMockupFontPixels = 13.0f;
		constexpr float kFooterHeightPixels = 24.0f;
		constexpr float kFooterTextPixels = 12.0f;
		constexpr float kFooterSidePaddingPixels = 10.0f;
		constexpr float kHalf = 0.5f;

		float MockupPixels(float pixels)
		{
			return pixels * ImGui::GetFontSize() / kMockupFontPixels;
		}

		struct FooterFont
		{
			FooterFont() { ImGui::PushFont(nullptr, MockupPixels(kFooterTextPixels)); }
			~FooterFont() { ImGui::PopFont(); }
			FooterFont(const FooterFont&) = delete;
			FooterFont& operator=(const FooterFont&) = delete;
		};

		// WARNING: The space the footer takes is its height whatever it holds, laid out by one item at the end, so the list above it never has to guess.
		// It starts where the list ends, in the item spacing ImGui put after the list: started below that spacing, it would
		// reach one spacing past the room the list left for it, and the history would scroll by that much both ways.
		struct FooterFrame
		{
			FooterFrame()
				: Start(ImGui::GetCursorPos().x, ImGui::GetCursorPos().y - ImGui::GetStyle().ItemSpacing.y),
				  Width(ImGui::GetContentRegionAvail().x)
			{
				const ImVec2 screen(ImGui::GetCursorScreenPos().x, ImGui::GetCursorScreenPos().y - ImGui::GetStyle().ItemSpacing.y);

				ImGui::GetWindowDrawList()->AddLine(screen, ImVec2(screen.x + Width, screen.y), ImGui::GetColorU32(ImGuiCol_Separator));

				const FooterFont font;
				const float textTop = std::floor((HistoryStatusLineHeight() - ImGui::GetTextLineHeight()) * kHalf);

				ImGui::SetCursorPos(ImVec2(Start.x + MockupPixels(kFooterSidePaddingPixels), Start.y + textTop));
			}

			~FooterFrame()
			{
				ImGui::SetCursorPos(Start);
				ImGui::Dummy(ImVec2(Width, HistoryStatusLineHeight()));
			}

			FooterFrame(const FooterFrame&) = delete;
			FooterFrame& operator=(const FooterFrame&) = delete;

			ImVec2 Start;
			float Width;
		};
		constexpr float kNoOffset = 0.0f;
		constexpr float kNoSpacing = 0.0f;

		float SmallButtonWidth(const char* label)
		{
			return ImGui::CalcTextSize(label).x + ImGui::GetStyle().FramePadding.x * kPaddingSides;
		}
	}

	float HistoryStatusLineHeight()
	{
		return std::floor(MockupPixels(kFooterHeightPixels));
	}

	std::optional<HistoryClick> DrawHistoryStatusLine(const HistoryStatus& status, std::string_view foldFooter)
	{
		std::optional<HistoryClick> clicked;

		const FooterFrame frame;
		const FooterFont font;

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

		// WARNING: Last of the text, so the total that lands later moves nothing before it.
		if (!status.Count.empty())
		{
			const bool followsText = saysText || !foldFooter.empty();

			if (saysText || !busy || !foldFooter.empty())
			{
				ImGui::SameLine(kNoOffset, kNoSpacing);
			}

			ImGui::TextDisabled("%s", ((followsText ? std::string{kBetween} : std::string{}) + status.Count).c_str());
		}

		const bool drewSomething = saysText || !busy || !foldFooter.empty() || !status.Count.empty();

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
		const float rightEdge = frame.Start.x + frame.Width - MockupPixels(kFooterSidePaddingPixels);
		ImGui::SetCursorPosX(std::max(textEnd, rightEdge - buttonsWidth));

		ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetColorU32(ImGuiCol_CheckMark));

		if (Core::SmallTextButton(more))
		{
			clicked = HistoryClick::More;
		}

		ImGui::SameLine();

		if (Core::SmallTextButton(all))
		{
			clicked = HistoryClick::All;
		}

		ImGui::PopStyleColor();

		return clicked;
	}
}

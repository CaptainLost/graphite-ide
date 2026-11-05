#include "Git/History/HistoryStatusLine.h"

#include "Core/ImGui/LoadingState.h"
#include "Core/ImGui/TextButton.h"
#include "UI/FittedText.h"

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

		const bool searching = status.Actions == HistoryActions::Search;
		const char* more = searching ? kSearchFurther : kLoadMore;
		const char* all = searching ? kSearchAll : kLoadAll;
		const bool offersActions = !busy && status.Actions != HistoryActions::None;
		const float buttonsWidth = SmallButtonWidth(more) + ImGui::GetStyle().ItemSpacing.x + SmallButtonWidth(all);
		const float rightEdge = frame.Start.x + frame.Width - MockupPixels(kFooterSidePaddingPixels);

		// WARNING: On a line too short for everything, the explanation gives way first, cut with an ellipsis, then the
		// fold offer's words, so the count, Fold them and the loader or the buttons stay in view on a narrow history.
		float kept = 0.0f;

		if (!foldFooter.empty())
		{
			kept += SmallButtonWidth(kFoldThem);
		}

		if (!status.Count.empty())
		{
			kept += ImGui::CalcTextSize((std::string{kBetween} + status.Count).c_str()).x;
		}

		if (busy)
		{
			kept += ImGui::GetStyle().ItemSpacing.x + Core::LoadingLineWidth(status.Busy.c_str());
		}
		else if (offersActions)
		{
			kept += ImGui::GetStyle().ItemSpacing.x + buttonsWidth;
		}

		const float room = std::max(0.0f, rightEdge - ImGui::GetCursorPosX() - kept);
		const std::string foldWhole = std::string{kBetween} + std::string{foldFooter};
		const float foldWidth = foldFooter.empty() ? 0.0f : ImGui::CalcTextSize(foldWhole.c_str()).x;

		const std::string text = FitToWidth(status.Text, std::max(0.0f, room - foldWidth));
		const bool saysText = !text.empty();
		const bool followsText = saysText || !foldFooter.empty();

		const std::string foldText = foldFooter.empty() ? std::string{}
								   : saysText			? foldWhole
														: FitToWidth(foldFooter, room);
		const std::string countText = (followsText ? std::string{kBetween} : std::string{}) + status.Count;

		// A busy line without text holds the loader alone.
		if (saysText || !busy)
		{
			ImGui::TextDisabled("%s", text.c_str());
		}

		if (!foldFooter.empty())
		{
			if (saysText || !busy)
			{
				ImGui::SameLine(kNoOffset, kNoSpacing);
			}

			ImGui::TextDisabled("%s", foldText.c_str());
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
			if (saysText || !busy || !foldFooter.empty())
			{
				ImGui::SameLine(kNoOffset, kNoSpacing);
			}

			ImGui::TextDisabled("%s", countText.c_str());
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

		if (!offersActions)
		{
			return clicked;
		}

		// WARNING: The buttons keep to the right edge but never cover the text: in a column too narrow
		// for both they follow it on the same line instead.
		ImGui::SameLine();
		const float textEnd = ImGui::GetCursorPosX();
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

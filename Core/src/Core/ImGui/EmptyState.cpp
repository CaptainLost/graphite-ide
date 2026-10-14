#include "corepch.h"

#include "Core/ImGui/EmptyState.h"

#include "Core/Base/Ascii.h"
#include "Core/ImGui/TextMetrics.h"

#include <imgui_internal.h>

#include <algorithm>

namespace Core {

	namespace {
		constexpr std::string_view kTrailingBlanks = " \t\r";
		constexpr float kNarrowestWrapWidth = 1.0f;
		constexpr float kTopFilledInLater = 0.0f;
		constexpr float kHalf = 0.5f;

		// WARNING: a block bigger than the space starts at the space's edge: centred regardless, its first line would sit above the top, where no scrolling reaches it.
		float CentredStart(float availableSpace, float blockExtent)
		{
			return std::max(0.0f, (availableSpace - blockExtent) * kHalf);
		}

		// WARNING: clamped because a window squeezed below its padding reports less than nothing left, and a negative Dummy would pull the cursor back over what came before.
		float RegionExtent(float requestedExtent, float availableExtent)
		{
			const float extent = requestedExtent > 0.0f ? requestedExtent : availableExtent;

			return std::max(extent, 0.0f);
		}

		float ButtonTop(const std::vector<CentredLine>& lines, float regionHeight, float buttonHeight, float itemSpacingHeight)
		{
			if (lines.empty())
			{
				return CentredStart(regionHeight, buttonHeight);
			}

			return lines.back().Offset.y + ImGui::GetTextLineHeight() + itemSpacingHeight;
		}
	}

	std::vector<CentredLine> CentreLines(std::string_view text, ImVec2 region, float below)
	{
		std::vector<CentredLine> lines;

		if (text.empty())
		{
			return lines;
		}

		ImFont* font = ImGui::GetFont();
		const float fontSize = ImGui::GetFontSize();

		// WARNING: a wrap width of nothing or less makes ImGui treat every character as a line of its own.
		const float wrapWidth = std::max(region.x, kNarrowestWrapWidth);

		const char* lineBegin = text.data();
		const char* const textEnd = lineBegin + text.size();

		while (lineBegin < textEnd)
		{
			const char* lineEnd = font->CalcWordWrapPosition(fontSize, lineBegin, textEnd, wrapWidth);

			// WARNING: a line that took nothing would never end this loop.
			const bool lineTookNothing = lineEnd == lineBegin && *lineBegin != '\n';
			if (lineTookNothing)
			{
				lineEnd = lineBegin + ImTextCountUtf8BytesFromChar(lineBegin, textEnd);
			}

			const std::string_view line =
				WithoutTrailing(std::string_view(lineBegin, static_cast<std::size_t>(lineEnd - lineBegin)), kTrailingBlanks);

			const float lineLeft = CentredStart(region.x, TextWidth(line));
			lines.push_back(CentredLine{.Text = line, .Offset = ImVec2(lineLeft, kTopFilledInLater)});

			lineBegin = ImTextCalcWordWrapNextLineStart(lineEnd, textEnd);
		}

		const float lineHeight = ImGui::GetTextLineHeight();
		const float blockTop =
			CentredStart(region.y, lineHeight * static_cast<float>(lines.size()) + below);

		for (std::size_t lineIndex = 0; lineIndex < lines.size(); ++lineIndex)
		{
			lines[lineIndex].Offset.y = blockTop + lineHeight * static_cast<float>(lineIndex);
		}

		return lines;
	}

	bool EmptyState(std::string_view message, const char* action, ImVec2 size)
	{
		const ImVec2 available = ImGui::GetContentRegionAvail();
		const ImVec2 region(RegionExtent(size.x, available.x), RegionExtent(size.y, available.y));
		const ImVec2 origin = ImGui::GetCursorScreenPos();
		const ImGuiStyle& style = ImGui::GetStyle();

		ImVec2 buttonSize(0.0f, 0.0f);
		float below = 0.0f;

		if (action != nullptr)
		{
			constexpr bool hideTextAfterDoubleHash = true;
			const ImVec2 sizeFromContent(0.0f, 0.0f);
			const float buttonWidth =
				ImGui::CalcTextSize(action, nullptr, hideTextAfterDoubleHash).x + style.FramePadding.x * 2.0f;

			buttonSize = ImGui::CalcItemSize(sizeFromContent, buttonWidth, ImGui::GetFrameHeight());
			below = style.ItemSpacing.y + buttonSize.y;
		}

		const std::vector<CentredLine> lines = CentreLines(message, region, below);

		ImDrawList* drawList = ImGui::GetWindowDrawList();
		const ImU32 colour = ImGui::GetColorU32(ImGuiCol_TextDisabled);

		for (const CentredLine& line : lines)
		{
			const ImVec2 lineOrigin(origin.x + line.Offset.x, origin.y + line.Offset.y);
			drawList->AddText(lineOrigin, colour, line.Text.data(), line.Text.data() + line.Text.size());
		}

		bool clicked = false;

		if (action != nullptr)
		{
			const float buttonTop = ButtonTop(lines, region.y, buttonSize.y, style.ItemSpacing.y);

			ImGui::SetCursorScreenPos(
				ImVec2(origin.x + CentredStart(region.x, buttonSize.x), origin.y + buttonTop));
			clicked = ImGui::Button(action, buttonSize);
		}

		const float lineHeight = ImGui::GetTextLineHeight();
		float blockHeight = below;
		if (!lines.empty())
		{
			blockHeight = lines.back().Offset.y + lineHeight + below;
		}

		// WARNING: a window whose last act was SetCursorScreenPos past its content fails ImGui's check on the way out, so this ends on a Dummy. It is never shorter than the message and button, or the window could not scroll to a block taller than the region.
		ImGui::SetCursorScreenPos(origin);
		ImGui::Dummy(ImVec2(region.x, std::max(region.y, blockHeight)));

		return clicked;
	}
}

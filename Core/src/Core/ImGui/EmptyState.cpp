#include "corepch.h"

#include "Core/ImGui/EmptyState.h"

#include <imgui_internal.h>

#include <algorithm>

namespace Core {

	namespace {
		// A block bigger than the space it is centred in starts at the space's edge: centred
		// regardless, its first line would sit above the top, where no scrolling reaches it.
		float CentredStart(float space, float extent)
		{
			return std::max(0.0f, (space - extent) * 0.5f);
		}

		std::string_view TrimTrailingBlanks(std::string_view line)
		{
			while (!line.empty() && (line.back() == ' ' || line.back() == '\t' || line.back() == '\r'))
			{
				line.remove_suffix(1);
			}

			return line;
		}

		float WidthOf(std::string_view text)
		{
			// Guarded because an empty view may carry no pointer at all.
			if (text.empty())
			{
				return 0.0f;
			}

			return ImGui::CalcTextSize(text.data(), text.data() + text.size()).x;
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
		const float size = ImGui::GetFontSize();

		// Never narrower than nothing: a window squeezed to its padding would otherwise wrap
		// at a negative width, which ImGui treats as one character a line.
		const float wrapWidth = std::max(region.x, 1.0f);

		const char* begin = text.data();
		const char* const end = begin + text.size();

		while (begin < end)
		{
			const char* lineEnd = font->CalcWordWrapPosition(size, begin, end, wrapWidth);

			// ImGui already cuts a word too long for a line, but a line that took nothing would
			// never end this loop.
			if (lineEnd == begin && *begin != '\n')
			{
				lineEnd = begin + ImTextCountUtf8BytesFromChar(begin, end);
			}

			const std::string_view line =
				TrimTrailingBlanks(std::string_view(begin, static_cast<std::size_t>(lineEnd - begin)));

			lines.push_back(CentredLine{line, ImVec2(CentredStart(region.x, WidthOf(line)), 0.0f)});

			begin = ImTextCalcWordWrapNextLineStart(lineEnd, end);
		}

		const float lineHeight = ImGui::GetTextLineHeight();
		const float top =
			CentredStart(region.y, lineHeight * static_cast<float>(lines.size()) + below);

		for (std::size_t index = 0; index < lines.size(); ++index)
		{
			lines[index].Offset.y = top + lineHeight * static_cast<float>(index);
		}

		return lines;
	}

	bool EmptyState(std::string_view message, const char* action, ImVec2 size)
	{
		const ImVec2 available = ImGui::GetContentRegionAvail();
		// Clamped because a window squeezed below its padding reports less than nothing left,
		// and a negative Dummy would pull the cursor back over what came before.
		const ImVec2 region(std::max(size.x > 0.0f ? size.x : available.x, 0.0f),
							std::max(size.y > 0.0f ? size.y : available.y, 0.0f));
		const ImVec2 origin = ImGui::GetCursorScreenPos();
		const ImGuiStyle& style = ImGui::GetStyle();

		ImVec2 buttonSize(0.0f, 0.0f);
		float below = 0.0f;

		if (action != nullptr)
		{
			buttonSize = ImGui::CalcItemSize(ImVec2(0.0f, 0.0f),
											 ImGui::CalcTextSize(action, nullptr, true).x + style.FramePadding.x * 2.0f,
											 ImGui::GetFrameHeight());
			below = style.ItemSpacing.y + buttonSize.y;
		}

		const std::vector<CentredLine> lines = CentreLines(message, region, below);

		ImDrawList* drawList = ImGui::GetWindowDrawList();
		const ImU32 colour = ImGui::GetColorU32(ImGuiCol_TextDisabled);

		for (const CentredLine& line : lines)
		{
			drawList->AddText(ImVec2(origin.x + line.Offset.x, origin.y + line.Offset.y), colour, line.Text.data(),
							  line.Text.data() + line.Text.size());
		}

		bool clicked = false;

		if (action != nullptr)
		{
			const float textBottom = lines.empty()
										 ? CentredStart(region.y, buttonSize.y)
										 : lines.back().Offset.y + ImGui::GetTextLineHeight() + style.ItemSpacing.y;

			ImGui::SetCursorScreenPos(
				ImVec2(origin.x + CentredStart(region.x, buttonSize.x), origin.y + textBottom));
			clicked = ImGui::Button(action, buttonSize);
		}

		// Last, and an item: a window whose final act was SetCursorScreenPos past its content
		// fails ImGui's check on the way out. Reset to the origin first so the claim covers
		// the region itself, not the region measured from under the button. Never shorter than
		// the message and its button: a block too tall for the region starts at its top, and
		// only a claim reaching its bottom lets the window scroll to the rest, and keeps what
		// the caller draws next off the button.
		const float lineHeight = ImGui::GetTextLineHeight();
		const float block = lines.empty() ? below : lines.back().Offset.y + lineHeight + below;

		ImGui::SetCursorScreenPos(origin);
		ImGui::Dummy(ImVec2(region.x, std::max(region.y, block)));

		return clicked;
	}
}

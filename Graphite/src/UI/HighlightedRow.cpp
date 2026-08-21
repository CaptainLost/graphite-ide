#include "UI/HighlightedRow.h"

#include "Core/Base/Ascii.h"

#include <imgui.h>

#include <string>

namespace Graphite {

	HighlightedLabel SplitHighlight(std::string_view label, std::string_view needle)
	{
		const std::string wanted = Core::Lowered(Core::Trimmed(needle));

		if (wanted.empty())
		{
			return HighlightedLabel{label, {}, {}};
		}

		const std::size_t at = Core::Lowered(label).find(wanted);

		if (at == std::string::npos)
		{
			return HighlightedLabel{label, {}, {}};
		}

		// Offsets from the lowered copy index the original directly: Lowered maps ASCII
		// letters one byte to one byte and leaves every other byte alone, so the two strings
		// have the same length and the same boundaries.
		return HighlightedLabel{label.substr(0, at), label.substr(at, wanted.size()),
								label.substr(at + wanted.size())};
	}

	namespace {

		// Both row drawers lay text over an empty Selectable, and both have to put the
		// cursor back where the row started to do it. The last piece each draws advances the
		// cursor exactly one line, which is what the Selectable advanced it by — so the row
		// is one line tall either way, and a clipper counting rows stays right.
		bool OverlaidSelectable(const char* id, bool selected, ImVec2& rowStart)
		{
			rowStart = ImGui::GetCursorPos();

			// AllowOverlap keeps the click reaching this rather than being swallowed by the
			// text items drawn on top of it.
			const bool clicked =
				ImGui::Selectable(id, selected, ImGuiSelectableFlags_AllowOverlap);

			ImGui::SetCursorPos(rowStart);
			return clicked;
		}
	}

	bool MarkedSelectable(const char* id, bool selected, std::string_view marker,
						  ImU32 markerColor, std::string_view text, std::string_view needle)
	{
		const HighlightedLabel split = SplitHighlight(text, needle);
		const bool colouredMarker = !marker.empty() && markerColor != 0;

		if (!colouredMarker && split.Match.empty())
		{
			// Nothing to lay over anything: one Selectable carrying its own label.
			return ImGui::Selectable((std::string(marker) + std::string(text)).c_str(),
									 selected);
		}

		ImVec2 rowStart;
		const bool clicked = OverlaidSelectable(id, selected, rowStart);

		if (!marker.empty())
		{
			if (markerColor != 0)
			{
				ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(markerColor), "%.*s",
								   static_cast<int>(marker.size()), marker.data());
			}
			else
			{
				// Drawn anyway, plainly: the row is being laid out in pieces for the
				// needle's sake, and a marker left out here would go missing from it.
				ImGui::TextUnformatted(marker.data(), marker.data() + marker.size());
			}

			ImGui::SameLine(0.0f, 0.0f);
		}

		ImGui::TextUnformatted(split.Before.data(), split.Before.data() + split.Before.size());

		if (!split.Match.empty())
		{
			ImGui::SameLine(0.0f, 0.0f);

			// The accent the theme already uses for a tick, so the match reads as marked
			// rather than as a second colour of text.
			ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_CheckMark), "%.*s",
							   static_cast<int>(split.Match.size()), split.Match.data());

			ImGui::SameLine(0.0f, 0.0f);
			ImGui::TextUnformatted(split.After.data(), split.After.data() + split.After.size());
		}

		return clicked;
	}

	bool HighlightedSelectable(const char* id, bool selected, std::string_view label,
							   std::string_view needle)
	{
		return MarkedSelectable(id, selected, {}, 0, label, needle);
	}
}

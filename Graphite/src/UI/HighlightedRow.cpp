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

	bool HighlightedSelectable(const char* id, bool selected, std::string_view label,
							   std::string_view needle)
	{
		const HighlightedLabel split = SplitHighlight(label, needle);

		if (split.Match.empty())
		{
			// Nothing to mark, so nothing to lay over anything: one Selectable carrying its
			// own label, which is what every other row in this application is.
			return ImGui::Selectable(std::string(label).c_str(), selected);
		}

		const ImVec2 rowStart = ImGui::GetCursorPos();

		// An empty label, drawn first, so the whole row is the click target and the text can
		// be laid over it in pieces. AllowOverlap keeps the click reaching this rather than
		// being swallowed by the text items on top.
		const bool clicked =
			ImGui::Selectable(id, selected, ImGuiSelectableFlags_AllowOverlap);

		// Back to where the row started, so the pieces are drawn over the Selectable rather
		// than under it. The last piece advances the cursor exactly one line, which is what
		// the Selectable advanced it by — so the row is one line tall either way, and a
		// clipper counting rows stays right.
		ImGui::SetCursorPos(rowStart);

		// The accent the theme already uses for a tick, so the match reads as marked rather
		// than as a second colour of text.
		const ImVec4 accent = ImGui::GetStyleColorVec4(ImGuiCol_CheckMark);

		ImGui::TextUnformatted(split.Before.data(), split.Before.data() + split.Before.size());
		ImGui::SameLine(0.0f, 0.0f);

		ImGui::TextColored(accent, "%.*s", static_cast<int>(split.Match.size()),
						   split.Match.data());
		ImGui::SameLine(0.0f, 0.0f);

		ImGui::TextUnformatted(split.After.data(), split.After.data() + split.After.size());

		return clicked;
	}
}

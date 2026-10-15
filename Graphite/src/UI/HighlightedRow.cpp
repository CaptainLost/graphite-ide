#include "UI/HighlightedRow.h"

#include "Core/Base/Narrow.h"

#include "Core/Base/Ascii.h"

#include <imgui.h>

#include <string>
#include <string_view>

namespace Graphite {

	namespace {
		constexpr ImU32 kNoMarkerColour = 0;
		constexpr float kNoSameLineOffset = 0.0f;
		constexpr float kNoSameLineSpacing = 0.0f;
		constexpr std::string_view kLabelIdSeparator = "##";

		void SameLineWithoutGap()
		{
			ImGui::SameLine(kNoSameLineOffset, kNoSameLineSpacing);
		}

		// WARNING: the last piece drawn over the Selectable must advance the cursor one line, as the
		// Selectable did, so the row is one line tall either way and a clipper counting rows stays right.
		bool OverlaidSelectable(const char* id, bool selected, ImVec2& rowStart)
		{
			rowStart = ImGui::GetCursorPos();

			const bool clicked = ImGui::Selectable(id, selected, ImGuiSelectableFlags_AllowOverlap);

			ImGui::SetCursorPos(rowStart);
			return clicked;
		}
	}

	HighlightedLabel SplitHighlight(std::string_view label, std::string_view needle)
	{
		const std::string loweredNeedle = Core::Lowered(Core::Trimmed(needle));

		if (loweredNeedle.empty())
		{
			return HighlightedLabel{.Before = label};
		}

		const std::size_t matchStart = Core::Lowered(label).find(loweredNeedle);

		if (matchStart == std::string::npos)
		{
			return HighlightedLabel{.Before = label};
		}

		// WARNING: offsets from the lowered copy index the original only because Lowered maps ASCII
		// letters one byte to one byte and leaves every other byte alone.
		return HighlightedLabel{.Before = label.substr(0, matchStart),
								.Match = label.substr(matchStart, loweredNeedle.size()),
								.After = label.substr(matchStart + loweredNeedle.size())};
	}

	void DrawHighlightedLabel(const HighlightedLabel& label, const ImVec4& matchColour)
	{
		ImGui::TextUnformatted(label.Before.data(), label.Before.data() + label.Before.size());

		if (label.Match.empty())
		{
			return;
		}

		SameLineWithoutGap();
		ImGui::TextColored(matchColour, "%.*s", Core::Narrow<int>(label.Match.size()), label.Match.data());

		SameLineWithoutGap();
		ImGui::TextUnformatted(label.After.data(), label.After.data() + label.After.size());
	}

	bool MarkedSelectable(const char* id, bool selected, std::string_view marker,
						  ImU32 markerColor, std::string_view text, std::string_view needle)
	{
		const HighlightedLabel split = SplitHighlight(text, needle);
		const bool hasColouredMarker = !marker.empty() && markerColor != kNoMarkerColour;
		const std::string wholeLabel = std::string(marker) + std::string(text);

		// WARNING: A Selectable hides its label from "##" on, so a label holding one is drawn over an
		// id-only Selectable like a marked one.
		const bool labelHidesPart = wholeLabel.find(kLabelIdSeparator) != std::string::npos;

		if (!hasColouredMarker && split.Match.empty() && !labelHidesPart)
		{
			return ImGui::Selectable(wholeLabel.c_str(), selected);
		}

		ImVec2 rowStart;
		const bool clicked = OverlaidSelectable(id, selected, rowStart);

		if (!marker.empty())
		{
			if (markerColor != kNoMarkerColour)
			{
				ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(markerColor), "%.*s",
								   Core::Narrow<int>(marker.size()), marker.data());
			}
			else
			{
				ImGui::TextUnformatted(marker.data(), marker.data() + marker.size());
			}

			SameLineWithoutGap();
		}

		DrawHighlightedLabel(split, ImGui::GetStyleColorVec4(ImGuiCol_CheckMark));

		return clicked;
	}

	bool HighlightedSelectable(const char* id, bool selected, std::string_view label,
							   std::string_view needle)
	{
		const std::string_view noMarker;

		return MarkedSelectable(id, selected, noMarker, kNoMarkerColour, label, needle);
	}
}

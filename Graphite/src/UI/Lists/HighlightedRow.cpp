#include "UI/Lists/HighlightedRow.h"

#include "Core/Base/Narrow.h"

#include "Core/Base/Ascii.h"

#include <imgui.h>

#include <string>
#include <string_view>

namespace Graphite {

	namespace {
		constexpr float kNoSameLineOffset = 0.0f;
		constexpr float kNoSameLineSpacing = 0.0f;

		void SameLineWithoutGap()
		{
			ImGui::SameLine(kNoSameLineOffset, kNoSameLineSpacing);
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
}

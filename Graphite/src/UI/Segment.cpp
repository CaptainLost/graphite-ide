#include "UI/Segment.h"

#include "Core/ImGui/TextButton.h"

#include <functional>

namespace Graphite {

	namespace {
		constexpr int kChosenSegmentColourCount = 4;
		constexpr ImVec4 kChosenSegmentText(1.0f, 1.0f, 1.0f, 1.0f);

		// WARNING: The chosen segment stays in the accent under the pointer and held: it is already the choice, and the
		// hover surface would read as taking it away.
		bool DrawSegmentWith(bool selected, const std::function<bool()>& button)
		{
			if (!selected)
			{
				return button();
			}

			const ImVec4 accent = ImGui::GetStyleColorVec4(ImGuiCol_CheckMark);

			ImGui::PushStyleColor(ImGuiCol_Button, accent);
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, accent);
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, accent);
			ImGui::PushStyleColor(ImGuiCol_Text, kChosenSegmentText);
			const bool clicked = button();
			ImGui::PopStyleColor(kChosenSegmentColourCount);

			return clicked;
		}
	}

	bool DrawSegment(const char* label, bool selected, const ImVec2& size)
	{
		return DrawSegmentWith(selected, [label, &size] { return Core::TextButton(label, size); });
	}

	bool DrawSmallSegment(const char* label, bool selected)
	{
		return DrawSegmentWith(selected, [label] { return Core::SmallTextButton(label); });
	}
}

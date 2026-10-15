#include "Search/HitList.h"

#include "Core/Base/Narrow.h"

#include "Search/HitDisplay.h"
#include "UI/OpenGesture.h"

#include <imgui.h>

#include <string_view>

namespace Graphite {

	namespace {

		constexpr float kNoOffsetFromStart = 0.0f;
		constexpr float kNoSpacing = 0.0f;
		constexpr const char* kNoLabel = "";

		void SameLineWithoutGap()
		{
			ImGui::SameLine(kNoOffsetFromStart, kNoSpacing);
		}

		bool IsHitSelected(const std::optional<HitSelection>& selected, const HitGroup& group,
						   std::size_t hitIndex)
		{
			return selected && selected->Group == group.Id && selected->Hit == hitIndex;
		}
	}

	void DrawHitList(const HitListInput& input)
	{
		const ImVec4 matchColor = ImGui::GetStyleColorVec4(ImGuiCol_CheckMark);

		for (HitGroup& group : input.Groups)
		{
			// WARNING: The id is the full path and never changes while results accumulate. The
			// label is drawn separately because its count changes on every new hit. Passing the
			// two combined as the id makes ImGui see a new node each time and close whatever the
			// user had just opened.
			const bool isGroupExpanded = ImGui::TreeNodeEx(
				group.Id.c_str(),
				ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_AllowOverlap,
				kNoLabel);

			SameLineWithoutGap();

			if (!group.Directory.empty())
			{
				ImGui::TextDisabled("%s", group.Directory.c_str());
				SameLineWithoutGap();
			}

			ImGui::TextUnformatted(group.Name.c_str());
			SameLineWithoutGap();
			ImGui::TextDisabled("  (%d)", Core::Narrow<int>(group.Hits.size()));

			if (!isGroupExpanded)
			{
				continue;
			}

			if (input.OnGroupOpened != nullptr)
			{
				(*input.OnGroupOpened)(group);
			}

			ImGuiListClipper clipper;
			clipper.Begin(Core::Narrow<int>(group.Hits.size()));

			while (clipper.Step())
			{
				for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
				{
					const std::size_t hitIndex = static_cast<std::size_t>(row);
					const SearchHit& hit = group.Hits[hitIndex];

					ImGui::PushID(row);

					const ImVec2 rowStart = ImGui::GetCursorPos();

					const bool isSelected = IsHitSelected(input.Selected, group, hitIndex);

					// WARNING: The empty selectable is drawn first so the whole row is the click
					// target, with the text laid over it in pieces. AllowOverlap keeps the click
					// reaching it instead of being swallowed by the text items on top.
					if (ImGui::Selectable("##hit", isSelected,
										  ImGuiSelectableFlags_AllowOverlap |
											  ImGuiSelectableFlags_AllowDoubleClick))
					{
						input.Selected = HitSelection{group.Id, hitIndex};

						if (OpensPermanently())
						{
							input.OnOpen(hit);
						}
					}

					const bool reachedByKeyboard = !isSelected && ImGui::IsItemFocused();

					if (reachedByKeyboard)
					{
						input.Selected = HitSelection{group.Id, hitIndex};
					}

					ImGui::SetCursorPos(rowStart);

					const HighlightedLabel line = SplitForDisplay(hit);

					const int displayedLineNumber = static_cast<int>(hit.Line + 1);

					ImGui::TextDisabled("%d: ", displayedLineNumber);
					SameLineWithoutGap();

					DrawHighlightedLabel(line, matchColor);

					ImGui::PopID();
				}
			}

			ImGui::TreePop();
		}
	}
}

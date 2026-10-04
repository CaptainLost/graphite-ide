#include "Search/HitList.h"

#include "Core/Base/Narrow.h"

#include "Search/HitDisplay.h"
#include "UI/OpenGesture.h"

#include <imgui.h>

namespace Graphite {

	void DrawHitList(const HitListInput& input)
	{
		// The accent the theme already uses for a tick, so the match reads as marked rather than
		// as a second colour nobody chose. When sub-project D makes the chrome data-driven this
		// follows it without a setting of its own.
		const ImVec4 accent = ImGui::GetStyleColorVec4(ImGuiCol_CheckMark);

		for (HitGroup& group : input.Groups)
		{
			// The node's id is the full path and never changes while results accumulate. Its
			// label is drawn separately, below, because the count in it changes on every new hit
			// — passing the two combined as the id, an earlier mistake here, made ImGui see a new
			// node each time and close whatever the user had just opened.
			const bool open = ImGui::TreeNodeEx(group.Id.c_str(),
												ImGuiTreeNodeFlags_SpanAvailWidth |
													ImGuiTreeNodeFlags_AllowOverlap,
												"");

			ImGui::SameLine(0.0f, 0.0f);

			// Dimmed, because the directory is the same for run after run of neighbouring rows
			// and the eye is looking for the file name.
			if (!group.Directory.empty())
			{
				ImGui::TextDisabled("%s", group.Directory.c_str());
				ImGui::SameLine(0.0f, 0.0f);
			}

			ImGui::TextUnformatted(group.Name.c_str());
			ImGui::SameLine(0.0f, 0.0f);
			ImGui::TextDisabled("  (%d)", Core::Narrow<int>(group.Hits.size()));

			if (!open)
			{
				continue;
			}

			// Before a single row of this group is submitted, and only for a group the user has
			// actually expanded: this is where a caller that has not yet read the lines its rows
			// show reads them, one file at a time, instead of reading every file a reply named in
			// the frame the reply arrived.
			if (input.OnGroupOpened != nullptr)
			{
				(*input.OnGroupOpened)(group);
			}

			// One expanded file can hold thousands of hits, and every row costs a string built to
			// draw it. The clipper submits only the rows the scroll actually shows, so an expanded
			// file costs what is on screen rather than what is in it.
			ImGuiListClipper clipper;
			clipper.Begin(Core::Narrow<int>(group.Hits.size()));

			while (clipper.Step())
			{
				for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
				{
					const SearchHit& hit = group.Hits[static_cast<std::size_t>(row)];

					ImGui::PushID(row);

					const ImVec2 rowStart = ImGui::GetCursorPos();

					const bool selected = input.Selected && input.Selected->Group == group.Id &&
										  input.Selected->Hit == static_cast<std::size_t>(row);

					// An empty label, drawn first, so the whole row is the click target and the
					// text can be laid over it in pieces. AllowOverlap keeps the click reaching
					// this rather than being swallowed by the text items on top;
					// AllowDoubleClick is what lets one row answer both gestures.
					if (ImGui::Selectable("##hit", selected,
										  ImGuiSelectableFlags_AllowOverlap |
											  ImGuiSelectableFlags_AllowDoubleClick))
					{
						input.Selected = HitSelection{group.Id, static_cast<std::size_t>(row)};

						// Single click previews, double click opens. That is the whole point:
						// arrow through fifty hits without leaving fifty tabs behind. Enter on
						// the row the arrows reached opens it too, as it does in Problems and
						// the file tree.
						if (OpensPermanently())
						{
							input.OnOpen(hit);
						}
					}

					// The preview follows the selection when it moves by keyboard too, which
					// arrives as nav focus rather than as a click.
					if (!selected && ImGui::IsItemFocused())
					{
						input.Selected = HitSelection{group.Id, static_cast<std::size_t>(row)};
					}

					ImGui::SetCursorPos(rowStart);

					// Three pieces rather than one string with a coloured rectangle behind it:
					// positioning that rectangle means measuring the text myself, and
					// CalcTextSize rounds up — the same drift that made the editor count whole
					// cells instead. Letting ImGui advance between pieces is exact by
					// construction.
					const HitLine line = SplitForDisplay(hit);

					ImGui::TextDisabled("%d: ", static_cast<int>(hit.Line + 1));
					ImGui::SameLine(0.0f, 0.0f);

					ImGui::TextUnformatted(line.Before.data(),
										   line.Before.data() + line.Before.size());
					ImGui::SameLine(0.0f, 0.0f);

					ImGui::TextColored(accent, "%.*s", Core::Narrow<int>(line.Match.size()),
									   line.Match.data());
					ImGui::SameLine(0.0f, 0.0f);

					ImGui::TextUnformatted(line.After.data(),
										   line.After.data() + line.After.size());

					ImGui::PopID();
				}
			}

			ImGui::TreePop();
		}
	}
}

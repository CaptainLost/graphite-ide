#include "Git/Window/NavigatorView.h"

#include "Theme/ActiveScheme.h"
#include "Theme/GitPalette.h"
#include "Theme/Palettes.h"
#include "UI/FittedText.h"
#include "UI/Lists/ListRow.h"

#include "Core/ImGui/Theme.h"

#include <IconsLucide.h>

#include <imgui.h>

namespace Graphite {

	namespace {

		constexpr float kDotRadiusInFontSizes = 0.2f;
		constexpr float kHalf = 0.5f;

		bool IsActionRow(NavigatorRowKind kind)
		{
			return kind != NavigatorRowKind::BranchesHeading && kind != NavigatorRowKind::Locked;
		}

		bool IsDimRow(NavigatorRowKind kind)
		{
			return kind == NavigatorRowKind::ShowAllWork || kind == NavigatorRowKind::ShowAllBranches ||
				   kind == NavigatorRowKind::Ready || kind == NavigatorRowKind::Locked;
		}

		// "3 ready to remove" reads as a sentence, the rest keep their number at the right edge.
		bool CountLeads(NavigatorRowKind kind)
		{
			return kind == NavigatorRowKind::Ready || kind == NavigatorRowKind::Locked;
		}

		void DrawText(const char* text, bool dim)
		{
			if (dim)
			{
				ImGui::TextDisabled("%s", text);
			}
			else
			{
				ImGui::TextUnformatted(text);
			}
		}

		// The icon in a row's lead slot; a thread's and the ready row's slot holds a dot instead.
		const char* LeadIcon(NavigatorRowKind kind)
		{
			switch (kind)
			{
			case NavigatorRowKind::Work:
				return ICON_LC_LAYOUT_GRID;
			case NavigatorRowKind::Changes:
				return ICON_LC_FILE_DIFF;
			case NavigatorRowKind::Branch:
				return ICON_LC_GIT_BRANCH;
			case NavigatorRowKind::Locked:
				return ICON_LC_LOCK;
			case NavigatorRowKind::Thread:
			case NavigatorRowKind::ShowAllWork:
			case NavigatorRowKind::Ready:
			case NavigatorRowKind::BranchesHeading:
			case NavigatorRowKind::ShowAllBranches:
				break;
			}

			return nullptr;
		}

		bool HasLeadSlot(NavigatorRowKind kind)
		{
			return kind != NavigatorRowKind::BranchesHeading;
		}
	}

	ImU32 WorkStateDotColour(WorkState state, const ResolvedScheme& scheme)
	{
		switch (state)
		{
		case WorkState::CanFastForward:
			return ReadGitPalette(scheme).GutterAdded;
		case WorkState::BehindTrunk:
			// WARNING: Not the gutter's modified colour, which is blue in the dark schemes.
			return ReadEditorPalette(scheme).DiagnosticWarning;
		case WorkState::Other:
			break;
		}

		return ReadGitPalette(scheme).LineNumber;
	}

	NavigatorView::NavigatorView(const ActiveScheme& scheme) : m_Scheme(scheme)
	{
	}

	void NavigatorView::Draw(const std::vector<NavigatorRow>& rows, const std::vector<Core::Branch>& branches)
	{
		const ResolvedScheme& scheme = m_Scheme.Get();
		const float spacing = ImGui::GetStyle().ItemSpacing.x;
		const float leadWidth = ImGui::CalcTextSize(ICON_LC_GIT_BRANCH).x;
		const float lineMiddle = ImGui::GetTextLineHeight() * kHalf;
		ImDrawList* const drawList = ImGui::GetWindowDrawList();

		for (std::size_t rowIndex = 0; rowIndex < rows.size(); ++rowIndex)
		{
			const NavigatorRow& row = rows[rowIndex];
			const bool dim = IsDimRow(row.Kind);
			const Core::Branch* const branch =
				row.Kind == NavigatorRowKind::Branch && row.Index < branches.size() ? &branches[row.Index] : nullptr;
			bool clicked = false;

			ImGui::PushID(static_cast<int>(rowIndex));

			const ImVec2 rowStart = ImGui::GetCursorScreenPos();
			const float textTop = ListRowTextTop(rowStart.y);

			if (IsActionRow(row.Kind))
			{
				clicked = ListRowSelectable("##row", row.Selected, ImGuiSelectableFlags_AllowOverlap);

				// The whole row answers the right click, not only its text.
				if (branch != nullptr && DrawBranchMenu)
				{
					DrawBranchMenu(*branch);
				}
			}

			ImGui::SetCursorScreenPos(ImVec2(rowStart.x, textTop));
			ImGui::BeginGroup();

			float labelLeft = HasLeadSlot(row.Kind) ? rowStart.x + leadWidth + spacing : rowStart.x;
			const ImVec2 leadCentre(rowStart.x + leadWidth * kHalf, textTop + lineMiddle);
			const float radius = ImGui::GetFontSize() * kDotRadiusInFontSizes;

			if (const char* const icon = LeadIcon(row.Kind); icon != nullptr)
			{
				ImGui::SetCursorScreenPos(ImVec2(rowStart.x, textTop));
				ImGui::TextUnformatted(icon);
			}
			else if (row.Kind == NavigatorRowKind::Thread || row.Kind == NavigatorRowKind::Ready)
			{
				const WorkState state = row.Kind == NavigatorRowKind::Thread ? row.State : WorkState::Other;
				drawList->AddCircleFilled(leadCentre, radius, WorkStateDotColour(state, scheme));
			}

			// HEAD's branch is marked with an accent dot before its name, which is drawn brighter.
			const bool isHead = branch != nullptr && branch->IsHead;

			if (isHead)
			{
				drawList->AddCircleFilled(ImVec2(labelLeft + radius, textTop + lineMiddle), radius,
										  ImGui::GetColorU32(ImGuiCol_CheckMark));
				labelLeft += 2.0f * radius + spacing;
			}

			const std::string count = row.Count;
			const float countWidth = count.empty() ? 0.0f : ImGui::CalcTextSize(count.c_str()).x;
			const float end = ListRowEnd();

			if (CountLeads(row.Kind) && !count.empty())
			{
				const std::string sentence = count + " " + row.Label;

				ImGui::SetCursorScreenPos(ImVec2(labelLeft, textTop));
				DrawText(FitToWidth(sentence, end - labelLeft).c_str(), dim);
			}
			else
			{
				const float room = end - labelLeft - (count.empty() ? 0.0f : countWidth + spacing);
				const std::string label = FitToWidth(row.Label, room);

				ImGui::SetCursorScreenPos(ImVec2(labelLeft, textTop));

				if (isHead)
				{
					ImGui::PushStyleColor(ImGuiCol_Text, Core::EmphasisText());
					ImGui::TextUnformatted(label.c_str());
					ImGui::PopStyleColor();
				}
				else
				{
					DrawText(label.c_str(), dim);
				}

				// The heading runs on as a rule to the row's end, as a section's does.
				if (row.Kind == NavigatorRowKind::BranchesHeading)
				{
					const float ruleLeft = ImGui::GetItemRectMax().x + spacing;

					if (ruleLeft < end)
					{
						drawList->AddLine(ImVec2(ruleLeft, textTop + lineMiddle), ImVec2(end, textTop + lineMiddle),
										  ImGui::GetColorU32(ImGuiCol_Separator));
					}
				}

				if (!count.empty())
				{
					ImGui::SetCursorScreenPos(ImVec2(end - countWidth, textTop));
					ImGui::TextDisabled("%s", count.c_str());
				}
			}

			ImGui::EndGroup();

			if (!IsActionRow(row.Kind))
			{
				// WARNING: A row of plain text is laid out as tall as a list row, or the rows below would stand short.
				ImGui::SetCursorScreenPos(rowStart);
				ImGui::ItemSize(ImVec2(0.0f, ListRowLineHeight()));
			}

			ImGui::PopID();

			if (!clicked)
			{
				continue;
			}

			if (row.Kind == NavigatorRowKind::Ready)
			{
				if (OnShowReady)
				{
					OnShowReady();
				}
			}
			else if (OnSelect)
			{
				OnSelect(SelectionOf(row));
			}
		}
	}
}

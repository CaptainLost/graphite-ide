#include "Git/Window/NavigatorView.h"

#include "Theme/ActiveScheme.h"
#include "Theme/GitPalette.h"
#include "Theme/Palettes.h"
#include "UI/FittedText.h"
#include "UI/Lists/ListRow.h"

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
				   kind == NavigatorRowKind::Ready || kind == NavigatorRowKind::Locked ||
				   kind == NavigatorRowKind::BranchesHeading;
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

		for (std::size_t rowIndex = 0; rowIndex < rows.size(); ++rowIndex)
		{
			const NavigatorRow& row = rows[rowIndex];
			const bool dim = IsDimRow(row.Kind);
			bool clicked = false;

			ImGui::PushID(static_cast<int>(rowIndex));

			const ImVec2 rowStart = ImGui::GetCursorScreenPos();
			const float textTop = ListRowTextTop(rowStart.y);

			if (IsActionRow(row.Kind))
			{
				clicked = ListRowSelectable("##row", row.Selected, ImGuiSelectableFlags_AllowOverlap);
			}

			ImGui::SetCursorScreenPos(ImVec2(rowStart.x, textTop));
			ImGui::BeginGroup();

			float labelLeft = rowStart.x;

			if (row.Kind == NavigatorRowKind::Thread)
			{
				const float radius = ImGui::GetFontSize() * kDotRadiusInFontSizes;
				const ImVec2 centre(rowStart.x + radius + spacing * kHalf, textTop + ImGui::GetTextLineHeight() * kHalf);

				ImGui::GetWindowDrawList()->AddCircleFilled(centre, radius, WorkStateDotColour(row.State, scheme));
				labelLeft = centre.x + radius + spacing;
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
				bool cut = false;

				ImGui::SetCursorScreenPos(ImVec2(labelLeft, textTop));
				DrawText(FitToWidth(row.Label, room, &cut).c_str(), dim);

				if (!count.empty())
				{
					ImGui::SetCursorScreenPos(ImVec2(end - countWidth, textTop));
					ImGui::TextDisabled("%s", count.c_str());
				}
			}

			ImGui::EndGroup();

			if (row.Kind == NavigatorRowKind::Branch && DrawBranchMenu && row.Index < branches.size())
			{
				DrawBranchMenu(branches[row.Index]);
			}

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

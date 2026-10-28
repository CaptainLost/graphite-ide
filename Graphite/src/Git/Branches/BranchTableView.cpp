#include "Git/Branches/BranchTableView.h"

#include "Git/Branches/BranchAge.h"
#include "UI/FittedText.h"
#include "UI/Lists/FilterField.h"
#include "UI/Lists/ListRow.h"
#include "UI/Segment.h"

#include "Core/ImGui/EmptyState.h"
#include "Core/ImGui/LoadingState.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <format>
#include <string>

namespace Graphite {

	namespace {

		// WARNING: The table keeps no settings. Sorted and never resized, ImGui saves the sorted column alone, and on
		// the next start it matches the columns saved with nothing to the others in turn and draws the sorted one first.
		constexpr ImGuiTableFlags kTableFlags = ImGuiTableFlags_Sortable | ImGuiTableFlags_ScrollY |
												ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV |
												ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings;

		// The table id is a constant, not BeginTable's hash of the label under the window, so a test can find it.
		constexpr const char* kTableName = "BranchTableView.Table";

		constexpr const char* kFilterId = "##branchTableFilter";
		constexpr const char* kNothingHere = "No branches here";
		constexpr const char* kNoTrunkDistance = "-";
		constexpr const char* kHeadMark = "* ";
		constexpr int kColumnCount = 6;
		constexpr std::size_t kScopeCount = 5;

		struct ScopeSpec
		{
			const char* Label;
			BranchScope Value;
		};

		constexpr std::array<ScopeSpec, kScopeCount> kScopes = {{
			{.Label = "All", .Value = BranchScope::All},
			{.Label = "Local", .Value = BranchScope::Local},
			{.Label = "Remote only", .Value = BranchScope::RemoteOnly},
			{.Label = "Mine", .Value = BranchScope::Mine},
			{.Label = "Stale", .Value = BranchScope::Stale},
		}};

		struct ColumnSpec
		{
			const char* Label;
			BranchColumn Id;
			ImGuiTableColumnFlags Flags;
			float Weight;
		};

		constexpr std::array<ColumnSpec, kColumnCount> kColumns = {{
			{.Label = "Branch", .Id = BranchColumn::Name, .Flags = ImGuiTableColumnFlags_None, .Weight = 3.0f},
			{.Label = "Where", .Id = BranchColumn::Where, .Flags = ImGuiTableColumnFlags_None, .Weight = 2.0f},
			{.Label = "Trunk", .Id = BranchColumn::Trunk, .Flags = ImGuiTableColumnFlags_None, .Weight = 1.5f},
			{.Label = "Age",
			 .Id = BranchColumn::Age,
			 .Flags = ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_PreferSortDescending,
			 .Weight = 1.0f},
			{.Label = "Authors", .Id = BranchColumn::Authors, .Flags = ImGuiTableColumnFlags_None, .Weight = 2.0f},
			{.Label = "Subject", .Id = BranchColumn::Subject, .Flags = ImGuiTableColumnFlags_None, .Weight = 4.0f},
		}};

		std::string TrunkText(const Core::PatchMatch& match)
		{
			return std::format("+{} -{}{}", match.Ahead, match.Behind, match.Complete ? "" : "+");
		}

		std::string AuthorsText(const BranchTableRow& row)
		{
			std::string text;

			for (const BranchAuthor& author : row.Authors)
			{
				text += text.empty() ? author.Name : ", " + author.Name;
			}

			return text;
		}

		void DrawFittedDisabled(const std::string& text)
		{
			const std::string fitted = FitToWidth(text, ImGui::GetContentRegionAvail().x);
			ImGui::TextDisabled("%s", fitted.c_str());
		}

		void DrawFitted(const std::string& text)
		{
			const std::string fitted = FitToWidth(text, ImGui::GetContentRegionAvail().x);
			ImGui::TextUnformatted(fitted.c_str());
		}
	}

	void BranchTableView::Reset()
	{
		m_Scope = BranchScope::All;
		m_SortBy = BranchColumn::Age;
		m_Ascending = false;
		m_Filter.clear();
		m_BuiltFrom.reset();
		m_Rows.clear();
		m_Counts = {};
	}

	void BranchTableView::RebuildWhenOutOfDate(const BranchTableInput& input)
	{
		if (m_BuiltFrom == input)
		{
			return;
		}

		m_Rows = BuildBranchTable(input);
		m_Counts = CountBranchScopes(input);
		m_BuiltFrom = input;
	}

	void BranchTableView::Draw(BranchTableInput input, const std::chrono::time_zone* zone)
	{
		if (input.Branches == nullptr)
		{
			Core::LoadingState("Loading branches");
			return;
		}

		// Ages read in minutes at the finest, so a clock that moves each frame does not rebuild the rows each frame.
		input.Now = std::chrono::floor<std::chrono::minutes>(input.Now);

		DrawFilterField(kFilterId, m_Filter, {.Hint = "Filter branches"});
		DrawScopes();

		input.Scope = m_Scope;
		input.SortBy = m_SortBy;
		input.Ascending = m_Ascending;
		input.Filter = m_Filter;

		RebuildWhenOutOfDate(input);
		DrawTable(input, zone);
	}

	void BranchTableView::DrawScopes()
	{
		for (std::size_t index = 0; index < kScopes.size(); ++index)
		{
			if (index > 0)
			{
				ImGui::SameLine();
			}

			const ScopeSpec& scope = kScopes[index];
			const std::string label = std::format("{} {}###scope{}", scope.Label, m_Counts[index], index);

			if (DrawSmallSegment(label.c_str(), scope.Value == m_Scope))
			{
				m_Scope = scope.Value;
			}
		}
	}

	void BranchTableView::DrawTable(const BranchTableInput& input, const std::chrono::time_zone* zone)
	{
		static const ImGuiID kTableId = ImHashStr(kTableName);

		ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ListTableCellPadding());

		// WARNING: A scrolling table fills the height it is not given, so with no row it is given its header's alone and
		// the empty state below it stays in view.
		const float headerOnly = ListRowPitch() + ImGui::GetStyle().ItemSpacing.y;
		const ImVec2 outerSize(0.0f, m_Rows.empty() ? headerOnly : 0.0f);

		if (!ImGui::BeginTableEx("##branchTable", kTableId, kColumnCount, kTableFlags, outerSize))
		{
			ImGui::PopStyleVar();
			return;
		}

		PadListTableEnds();

		for (const ColumnSpec& column : kColumns)
		{
			ImGui::TableSetupColumn(column.Label, column.Flags, column.Weight, static_cast<ImGuiID>(column.Id));
		}

		ImGui::TableSetupScrollFreeze(0, 1);

		// The table owns the sort; the view keeps what it reads, and the next rebuild compares it with the input.
		if (ImGuiTableSortSpecs* const sortSpecs = ImGui::TableGetSortSpecs(); sortSpecs != nullptr)
		{
			if (sortSpecs->SpecsCount > 0)
			{
				const ImGuiTableColumnSortSpecs& spec = sortSpecs->Specs[0];
				m_SortBy = static_cast<BranchColumn>(spec.ColumnUserID);
				m_Ascending = spec.SortDirection != ImGuiSortDirection_Descending;
			}

			sortSpecs->SpecsDirty = false;
		}

		ImGui::TableHeadersRow();

		ImGuiListClipper clipper;
		clipper.Begin(static_cast<int>(m_Rows.size()), ListRowPitch());

		while (clipper.Step())
		{
			for (int rowIndex = clipper.DisplayStart; rowIndex < clipper.DisplayEnd; ++rowIndex)
			{
				DrawRow(m_Rows[static_cast<std::size_t>(rowIndex)], input, zone);
			}
		}

		clipper.End();

		ImGui::EndTable();
		ImGui::PopStyleVar();

		if (m_Rows.empty())
		{
			Core::EmptyState(kNothingHere);
		}
	}

	void BranchTableView::DrawRow(const BranchTableRow& row, const BranchTableInput& input,
								  const std::chrono::time_zone* zone)
	{
		const std::vector<Core::Branch>& branches = *input.Branches;
		const Core::Branch& branch = row.Local.has_value() ? branches[*row.Local] : branches[*row.Remote];

		ImGui::TableNextRow();
		ImGui::PushID(branch.FullName.c_str());
		ImGui::TableSetColumnIndex(0);

		const bool clicked = TableRowSelectable("##row", false, ImGuiSelectableFlags_AllowOverlap);
		const bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip);

		if (DrawBranchMenu)
		{
			DrawBranchMenu(branch);
		}

		DrawFitted(row.IsHead ? kHeadMark + row.Name : row.Name);

		ImGui::TableSetColumnIndex(1);
		DrawFittedDisabled(WhereText(row));

		ImGui::TableSetColumnIndex(2);

		if (row.Trunk.has_value())
		{
			DrawFittedDisabled(TrunkText(*row.Trunk));
		}
		else if (input.Facts == nullptr && row.Local.has_value())
		{
			Core::LoadingLine("");
		}
		else
		{
			DrawFittedDisabled(kNoTrunkDistance);
		}

		ImGui::TableSetColumnIndex(3);

		if (row.TipTime.has_value())
		{
			DrawFittedDisabled(FormatBranchAge(*row.TipTime, input.Now));
		}

		ImGui::TableSetColumnIndex(4);
		DrawFittedDisabled(AuthorsText(row));

		ImGui::TableSetColumnIndex(5);
		DrawFittedDisabled(row.Subject);

		if (hovered)
		{
			ImGui::SetTooltip("%s", BranchTooltipText(branch, row.Name, row.Authors, zone).c_str());
		}

		ImGui::PopID();

		if (clicked && OnBranchClicked)
		{
			OnBranchClicked(branch);
		}
	}
}

#pragma once

#include "Git/Branches/BranchAuthors.h"
#include "Git/Branches/BranchListFilter.h"
#include "Git/Branches/BranchRows.h"
#include "Persistence/PersistedState.h"

#include "Core/Base/Base.h"
#include "Core/Git/Ref.h"

#include <array>
#include <chrono>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace Graphite {

	struct BranchListFrame
	{
		Core::Ref<const std::vector<Core::Branch>> Branches;
		bool BranchesFailed = false;

		Core::Ref<const BranchAuthors> Authors;
		std::optional<std::string> AuthorsFailure;

		std::optional<std::string> SelectedRef;

		std::chrono::sys_seconds Now;
		std::chrono::minutes LocalOffset{0};
	};

	class BranchListView
	{
	public:
		explicit BranchListView(BranchListState& options);

		std::function<void(const Core::Branch& branch)> OnBranchClicked;
		std::function<void(const Core::Branch& branch)> DrawBranchMenu;
		std::function<std::string(const Core::Branch& branch)> BadgeOf;

		void DrawHeader(const BranchListFrame& frame);

		// The caller opens the child window these rows scroll in.
		void DrawRows(const BranchListFrame& frame);

		void Reset();

	private:
		struct RowsBuiltFrom
		{
			Core::Ref<const std::vector<Core::Branch>> Branches;
			Core::Ref<const BranchAuthors> Authors;
			std::optional<std::string> AuthorsFailure;
			BranchListFilter Filter;
			BranchListState Options;
			std::array<bool, kBranchSectionCount> CollapsedSections{};
			std::set<AuthorGroupKey> ExpandedGroups;

			friend bool operator==(const RowsBuiltFrom&, const RowsBuiltFrom&) = default;
		};

		RowsBuiltFrom InputsOf(const BranchListFrame& frame) const;
		static BranchRowsInput RowsInputOf(const RowsBuiltFrom& inputs);
		void RebuildRowsWhenOutOfDate(const BranchListFrame& frame);

		void DrawMenu(const BranchListFrame& frame);
		void DrawSortItem(const char* label, BranchSort sort);
		void DrawGroupingItem(const char* label, BranchGrouping grouping);
		void DrawAuthorsMenu(const BranchListFrame& frame);
		void DrawOnlyMineItem(const BranchListFrame& frame);
		void DrawChips(const BranchListFrame& frame);
		void PlaceChip(const std::string& label, bool& isFirstChip, float rowRight) const;
		std::string ChipTextOf(const std::string& key, const BranchListFrame& frame) const;

		void DrawRow(const BranchRow& row, const BranchListFrame& frame);
		void DrawBranchRow(const BranchRow& row, const BranchListFrame& frame);
		void DrawAuthorGroupRow(const BranchRow& row);
		void DrawWaitingRow(const BranchRow& row);

		void ToggleAuthor(const std::string& key);

		BranchListState& m_Options;

		std::array<char, 128> m_TextBuffer{};
		BranchListFilter m_Filter;
		std::vector<std::string> m_Words;

		std::array<bool, kBranchSectionCount> m_CollapsedSections{};
		std::set<AuthorGroupKey> m_ExpandedGroups;

		std::optional<RowsBuiltFrom> m_RowsBuiltFrom;
		std::vector<BranchRow> m_Rows;
		std::vector<AuthorChoice> m_Choices;

		std::optional<double> m_BranchesWaitingSince;
		std::optional<double> m_AuthorsWaitingSince;
		std::optional<double> m_AuthorsMenuWaitingSince;
	};
}
#pragma once

#include "Git/Branches/BranchTable.h"

#include "Core/Git/Ref.h"

#include <array>
#include <chrono>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace Graphite {

	class BranchTableView
	{
	public:
		std::function<void(const Core::Branch& branch)> OnBranchClicked; // the local branch when there is one
		std::function<void(const Core::Branch& branch)> DrawBranchMenu;

		// Owns the scope, the sort and the filter; rebuilds rows only when the input or those change.
		// WARNING: The input's branches, authors and facts are compared by address, so the caller hands a new
		// object whenever their contents change.
		void Draw(BranchTableInput input, const std::chrono::time_zone* zone);

		void Reset();

	private:
		void RebuildWhenOutOfDate(const BranchTableInput& input);
		void DrawScopes();
		void DrawTable(const BranchTableInput& input, const std::chrono::time_zone* zone);
		void DrawRow(const BranchTableRow& row, const BranchTableInput& input, const std::chrono::time_zone* zone);

		BranchScope m_Scope = BranchScope::All;
		BranchColumn m_SortBy = BranchColumn::Age;
		bool m_Ascending = false;
		std::string m_Filter;

		std::optional<BranchTableInput> m_BuiltFrom;
		std::vector<BranchTableRow> m_Rows;
		std::array<std::size_t, 5> m_Counts{};
	};
}

#pragma once

#include "Git/Work/ChipRow.h"
#include "Git/Work/WorkBoard.h"
#include "Review/ReviewFiles.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Graphite {

	struct ThreadHeaderInput
	{
		// May be null: a branch with no card (merged).
		const WorkCard* Card = nullptr;
		std::string_view TrunkName;

		// The session's base, named only when it is not the trunk.
		std::optional<std::string> AgainstBase;

		// The thread's branch, full name: what the header is named after when there is no card.
		std::string_view Branch;

		// Empty while the count is unknown.
		std::optional<ViewedCount> Viewed;

		// Why the count will not come. The header then shows no count: the review below says what is wrong.
		std::optional<std::string> ReviewProblem;

		ChipColours Colours;
	};

	enum class ThreadHeaderClick
	{
		None,
		BackToWork,
		OpenInOwnTab
	};

	// The card's chips, the files viewed unless the count will not come, and "against {base}" when the base is not the
	// trunk.
	std::vector<Chip> ThreadHeaderChips(const ThreadHeaderInput& input);

	// Back to Work, the title, branch and folder, the card chips, "against {base}" when the session's base is not the
	// trunk, and "Open in own tab". Answers which button was clicked.
	ThreadHeaderClick DrawThreadHeader(const ThreadHeaderInput& input);
}

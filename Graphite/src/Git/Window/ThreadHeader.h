#pragma once

#include "Git/Work/WorkBoard.h"
#include "Review/ReviewFiles.h"

#include <optional>
#include <string>
#include <string_view>

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
	};

	// Title, branch and folder, the card chips, "against {base}" when the session's base is not the trunk, and
	// "Open in own tab". True when "Open in own tab" was clicked.
	bool DrawThreadHeader(const ThreadHeaderInput& input);
}

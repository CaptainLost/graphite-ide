#pragma once

#include <optional>
#include <string_view>

namespace Graphite {

	// What the commit button needs to know to decide whether it can be pressed. A struct
	// rather than four adjacent bools: a caller could transpose those silently (ADR 0035).
	struct CommitBlockerInputs
	{
		bool WriteInFlight = false;
		bool AnythingStaged = false;

		// A merge may legitimately record HEAD's own tree, so it needs no staged change.
		bool Merging = false;

		bool MessageIsEmpty = true;
		bool Amend = false;
	};

	// Why the commit button is disabled, worded for its tooltip, or nothing when it can be
	// pressed. One function answers both questions so the button and its explanation cannot
	// disagree about whether it is enabled.
	std::optional<std::string_view> WhyCommitIsDisabled(const CommitBlockerInputs& inputs);
}

#pragma once

#include "Git/CommitDetails.h"
#include "Git/GitPalette.h"

#include "Core/Git/Commit.h"

#include <chrono>
#include <optional>

namespace Graphite {

	// The pane under the commit list, for whichever commit is selected: who wrote it and
	// when, what it is descended from, how much it changed, and the whole message.
	//
	// A free function rather than a class, because there is nothing to remember between
	// frames — every line is a function of the commit it is handed. The one piece of state
	// this would otherwise hold, which commit to draw, belongs to the list that has the
	// selection.
	//
	// `stats` absent means the commit's diff has not arrived (or was evicted from the
	// model's cache); the line still draws, dimmed, so the pane does not change height
	// under the reader when the answer lands.
	//
	// `offset` is the reader's own offset from UTC, resolved by the caller once a frame —
	// see FormattedTimestamp for why it is not read here.
	//
	// Returns whether the reader pressed the pane's close. Reported rather than acted on,
	// because dismissing a selection is the business of whoever holds it — this function
	// knows only the commit it was handed.
	bool RenderCommitDetails(const Core::CommitSummary& commit,
							 const std::optional<DiffStats>& stats, std::chrono::minutes offset,
							 const GitPalette& palette);
}

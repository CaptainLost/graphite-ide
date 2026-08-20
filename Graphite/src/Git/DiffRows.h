#pragma once

#include "Core/Git/Diff.h"

#include <cstddef>
#include <optional>
#include <vector>

namespace Graphite {

	enum class DiffRowKind
	{
		// The "@@ -a,b +c,d @@" banner that opens a hunk.
		HunkHeader,
		Context,
		Added,
		Removed
	};

	// One drawable row. Carries indices rather than the text itself: the widget already
	// holds the snapshot the diff came from, and copying every line into the rows would
	// double a large diff in memory for no gain. A row is valid only against the exact
	// FileDiff it was built from — a caller must rebuild its rows whenever its snapshot
	// changes, because a stale row's indices can still be in range against a newer diff,
	// in which case LineOf resolves it to the wrong line rather than to nothing.
	struct DiffRow
	{
		DiffRowKind Kind = DiffRowKind::Context;

		// Zero where the line does not exist on that side, matching Core::DiffLine.
		int OldLine = 0;
		int NewLine = 0;

		// Index into FileDiff::Hunks. Meaningful for every kind, including the header,
		// because staging acts on a whole hunk.
		std::size_t Hunk = 0;

		// Index into Hunk::Lines. Meaningless for a header row; use LineOf rather than
		// indexing with it directly.
		std::size_t Line = 0;
	};

	// One row of the side-by-side view. Either half may be absent: a line that was only
	// deleted has no right half, and one that was only inserted has no left.
	struct DiffPair
	{
		std::optional<DiffRow> Left;
		std::optional<DiffRow> Right;
	};

	// The diff's own order, hunk headers included. This is what a reviewer reading a patch
	// expects, and it is what the unified view draws.
	std::vector<DiffRow> BuildUnifiedRows(const Core::FileDiff& file);

	// The same content, with each removed line paired against the added line that replaced
	// it, so an edit reads as one row rather than as a deletion above an insertion.
	std::vector<DiffPair> BuildSideBySideRows(const Core::FileDiff& file);

	// The line a row draws, or null for a hunk header and for a row whose indices no longer
	// fit the file — which can only happen if a caller keeps rows past the snapshot they
	// were built from.
	const Core::DiffLine* LineOf(const Core::FileDiff& file, const DiffRow& row);
}

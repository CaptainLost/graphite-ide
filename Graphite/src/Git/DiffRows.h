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
		Removed,

		// A line the diff never mentioned at all: outside every hunk, so it belongs to
		// no hunk index and is never offered for staging. Distinct from Context, which
		// is a line the diff chose to show around a change and does belong to one.
		Unchanged
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

		// Index into FileDiff::Hunks. Meaningful for every kind but Unchanged, including the
		// header, because staging acts on a whole hunk. An Unchanged row belongs to no hunk
		// at all, so this is left at its default and must not be read.
		std::size_t Hunk = 0;

		// Index into Hunk::Lines. Meaningless for a header row and for Unchanged; use LineOf
		// rather than indexing with it directly.
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

	// The line a row draws, or null for a hunk header, for an Unchanged row (which names no
	// hunk to look a line up in), and for a row whose indices no longer fit the file — which
	// can only happen if a caller keeps rows past the snapshot they were built from.
	const Core::DiffLine* LineOf(const Core::FileDiff& file, const DiffRow& row);

	// Both views of a whole file, hunks in place and Unchanged rows filling every gap.
	struct WholeFileRows
	{
		std::vector<DiffRow> Unified;
		std::vector<DiffPair> SideBySide;
	};

	// Lays out every line of the file, not just the hunks: the lines a hunk covers keep the
	// rows BuildUnifiedRows/BuildSideBySideRows would give them, and every line outside a hunk
	// becomes an Unchanged row. Takes line counts rather than the texts themselves — the
	// builder only decides which lines exist and how they pair, never what they say — so the
	// widget looks a line's text up by number exactly as it already looks a diff line up by
	// index.
	WholeFileRows BuildWholeFileRows(const Core::FileDiff& file, std::size_t oldLineCount,
									  std::size_t newLineCount);
}

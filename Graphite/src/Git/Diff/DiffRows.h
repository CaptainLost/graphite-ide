#pragma once

#include "Git/Diff/SyntaxRuns.h"

#include "Core/Git/Diff.h"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Graphite {

	enum class DiffRowKind
	{
		// The "@@ -a,b +c,d @@" banner that opens a hunk.
		HunkHeader,

		// The enclosing scope git named after the header's second "@@", on a row of its own
		// under it. Only for a header that names one; like the header, it names no line.
		HunkSignature,

		// The blank row ahead of every hunk's header but the first, in the hunks-only rows:
		// it stands for the lines between two hunks that those rows leave out. Names no line,
		// like the header, and belongs to the hunk it opens.
		HunkGap,
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

	// How much of the file a diff shows: only the hunks, or every line with the hunks in place.
	enum class DiffScope
	{
		Hunks,
		WholeFile
	};

	// The diff's own order, hunk headers included, with a gap row between one hunk and the
	// next. This is what a reviewer reading a patch expects, and it is what the unified view
	// draws.
	std::vector<DiffRow> BuildUnifiedRows(const Core::FileDiff& file);

	// The same content, with each removed line paired against the added line that replaced
	// it, so an edit reads as one row rather than as a deletion above an insertion.
	std::vector<DiffPair> BuildSideBySideRows(const Core::FileDiff& file);

	// The line a row draws, or null for a hunk header or gap, for an Unchanged row (which
	// names no hunk to look a line up in), and for a row whose indices no longer fit the file
	// — which can only happen if a caller keeps rows past the snapshot they were built from.
	const Core::DiffLine* LineOf(const Core::FileDiff& file, const DiffRow& row);

	// Both views of a whole file, hunks in place and Unchanged rows filling every gap.
	struct WholeFileRows
	{
		std::vector<DiffRow> Unified;
		std::vector<DiffPair> SideBySide;
	};

	// Lays out every line of the file, not just the hunks: the lines a hunk covers keep the
	// rows BuildUnifiedRows/BuildSideBySideRows would give them, and every line outside a hunk
	// becomes an Unchanged row — so no gap rows, since nothing between the hunks is left out.
	// Takes line counts rather than the texts themselves — the builder only decides which
	// lines exist and how they pair, never what they say — so the widget looks a line's text
	// up by number exactly as it already looks a diff line up by index.
	WholeFileRows BuildWholeFileRows(const Core::FileDiff& file, std::size_t oldLineCount,
									 std::size_t newLineCount);

	// Which half of the side-by-side view a row is being resolved for. The unified view has one
	// text column standing for both sides and asks for New, which is what it drew before this
	// distinction existed.
	enum class DiffSide
	{
		Old,
		New
	};

	// What a row draws: the text — absent for a row whose indices no longer fit the file,
	// matching LineOf — and the tokens that colour it, null when there is nothing to colour it
	// with.
	struct ResolvedLine
	{
		std::optional<std::string_view> Text;
		const std::vector<Token>* Tokens = nullptr;
	};

	// Where the whole file's texts, the tokens lexed from them and the rows built over them
	// finally meet: side selection, the one-based row numbers turned into zero-based indices,
	// and a bounds check against each of the four vectors that can disagree about how long the
	// file is. A free function taking plain arguments rather than a widget's private method,
	// because this arithmetic decides what gets drawn and has to be reachable by a test.
	//
	// `oldLines`/`newLines` are the file's lines as SplitTextLines produced them, empty for a
	// side the file does not have; `tokens` are the tokens lexed from those same lines. `side`
	// is preferred whenever it has the line, so each half of a rename that changes the
	// extension is coloured by its own language's lexer rather than by the new file's.
	ResolvedLine ResolveRow(const Core::FileDiff& file, const DiffRow& row, DiffSide side,
							std::span<const std::string_view> oldLines,
							std::span<const std::string_view> newLines,
							const FileTokens& tokens);

	// A line of the file, named by its number on one side, which a view can find again after
	// its rows are rebuilt in another layout — hunks-only or whole file, unified or side by
	// side — so that switching between them leaves the reader looking at the same line
	// rather than wherever the old scroll position happens to land in the new rows.
	struct DiffLineAnchor
	{
		DiffSide Side = DiffSide::New;
		int Line = 0;

		bool operator==(const DiffLineAnchor& other) const = default;
	};

	// The line a row stands for: its new-side number when it has one, since that is the file
	// as it is now, and its old-side number for a line that was only deleted. Absent for a
	// hunk header, which is no line of the file at all.
	std::optional<DiffLineAnchor> AnchorOf(const DiffRow& row);

	// The same for a side-by-side row: the right half's line when it has one, else the left's.
	std::optional<DiffLineAnchor> AnchorOf(const DiffPair& pair);

	// Where an anchor lands in a layout's rows. `Exact` is false when the line is not among
	// them — a line outside every hunk, looked for in the hunks-only rows — in which case
	// `Row` is the first row past it, stepped back over the rows just before that one with no
	// line on the anchor's side — its hunk's header, and lines only the other side has — so the
	// reader sees the hunk begin rather than arriving just inside it. Past the last row it is
	// the last row; in no rows at all it is zero.
	struct DiffRowAt
	{
		std::size_t Row = 0;
		bool Exact = false;

		bool operator==(const DiffRowAt& other) const = default;
	};

	DiffRowAt RowAt(std::span<const DiffRow> rows, DiffLineAnchor anchor);
	DiffRowAt RowAt(std::span<const DiffPair> pairs, DiffLineAnchor anchor);

	// Which of the rows on screen, `first` up to but not including `end`, a switch of layout
	// should keep where it is. The one nearest the middle of the view, since that is where the
	// reader is looking rather than the top edge — and among those, first a row of a hunk,
	// which every layout has, over an Unchanged line that the hunks-only one leaves out:
	// keeping a change in place matters more than keeping a line near it that is about to
	// vanish. Absent when no row on screen is a line of the file at all.
	std::optional<std::size_t> RowToKeep(std::span<const DiffRow> rows, std::size_t first,
										 std::size_t end);
	std::optional<std::size_t> RowToKeep(std::span<const DiffPair> pairs, std::size_t first,
										 std::size_t end);

	// What a measured width belongs to.
	//
	// The side-by-side view keeps the widest line it has drawn rather than remeasuring the
	// visible rows each frame, so that scrolling down past a long line does not shrink the
	// scrollbar out from under the reader. That only holds while the rows are the same rows:
	// once they are a different diff's, the kept width is a promise of content that is no
	// longer there, and ImGui leaves the reader scrolled into blank space with nothing to
	// suggest scrolling back.
	//
	// The path alone does not answer it. One DiffView serves every file of every commit, so
	// the same path in the next commit arrives with the path unchanged and the lines entirely
	// different — which is exactly the case a path-keyed reset misses.
	struct DiffMeasurement
	{
		std::filesystem::path Path;
		DiffScope Scope = DiffScope::Hunks;
		std::size_t Hunks = 0;
		std::size_t Lines = 0;
		int LastOldLine = 0;
		int LastNewLine = 0;

		bool operator==(const DiffMeasurement& other) const = default;
	};

	// Cheap enough to build every frame: it counts hunks and lines and reads the last line's
	// numbers, rather than hashing the text. Two diffs it cannot tell apart agree on the path,
	// the hunk count, the line count and where the last line sits in both files — in which
	// case they are near enough the same shape that a width measured for one fits the other.
	DiffMeasurement MeasurementOf(const Core::FileDiff& file, DiffScope scope);

	// The enclosing scope git puts after a hunk header's second "@@" — the nearest line above
	// the change that looks like the start of a function or a namespace — without the line
	// break. Empty when the header names none, or is not shaped like a header at all.
	std::string_view HunkScope(std::string_view header);

	// The line of `lines` git found `scope` on: the nearest one above line `before` — a hunk's
	// start — that begins with it, indentation aside, since git may have cut a long line short
	// and HunkScope trims whatever indentation it kept. Zero when no line matches, which is
	// also the answer for an empty scope. One-based, like every line number here.
	int SignatureLine(std::span<const std::string_view> lines, int before, std::string_view scope);

	// The lines a hunk covers on one side, as "first–last", or the one number when it covers
	// one line. A dash when it covers none: that side's start then names only where the other
	// side's change sits, not a line of its own.
	std::string LineRangeLabel(int start, int count);
}

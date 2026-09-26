#pragma once

#include "Core/Git/TextDiff.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Graphite {

	enum class ChangeKind : std::uint8_t
	{
		None,
		Added,
		Modified
	};

	// One buffer line's marker. Staged is the dimmed shade: the line is as the index has it,
	// and the index differs from HEAD there.
	struct LineChange
	{
		ChangeKind Kind = ChangeKind::None;
		bool Staged = false;

		friend bool operator==(const LineChange&, const LineChange&) = default;
	};

	// Lines removed at a boundary. Line is the buffer line the marker is drawn above; one past
	// the last line means the removal was at the very end.
	struct DeletionAnchor
	{
		std::size_t Line = 0;
		bool Staged = false;

		friend bool operator==(const DeletionAnchor&, const DeletionAnchor&) = default;
	};

	enum class HunkKind : std::uint8_t
	{
		Added,
		Modified,
		Deleted
	};

	// How many of a hunk's old lines are kept for the tooltip. Past this the reader is told how
	// many more there are rather than shown them: a tooltip is not the place to read a rewrite.
	inline constexpr std::size_t kShownOldLines = 15;

	// One drawn change, as the tooltip over its marker describes it.
	struct ChangeHunk
	{
		HunkKind Kind = HunkKind::Added;
		bool Staged = false;

		// Added and Modified: the first buffer line the hunk marks, and how many lines from there
		// its markers span. Deleted: the anchor line, with LineCount zero.
		//
		// A staged hunk's span runs from its first marked line to its last. An unstaged insertion
		// in the middle of it splits the markers without splitting the span, which is why a
		// lookup also matches the shade the line is drawn in.
		std::size_t FirstLine = 0;
		std::size_t LineCount = 0;

		// How many lines this hunk's own markers occupy, which is what a reader counts. Smaller
		// than LineCount when unstaged lines split a staged hunk, or rewrote some of it.
		std::size_t MarkedCount = 0;

		// Every line the hunk introduced is still marked, contiguously from FirstLine, so its
		// n-th new line is buffer line FirstLine + n. Only then can an old line be paired with
		// the new line in its place.
		bool Intact = true;

		// The lines the hunk replaced or removed, from the index for an unstaged hunk and from
		// HEAD for a staged one. OldCount is all of them; OldLines only the first kShownOldLines.
		std::size_t OldCount = 0;
		std::vector<std::string> OldLines;
	};

	struct ChangeMarkers
	{
		// Exactly one entry per buffer line.
		std::vector<LineChange> Lines;

		// Ascending by Line, and never two on one line.
		std::vector<DeletionAnchor> Deletions;

		// The hunks the markers above come from, for the tooltip. A staged hunk whose every line
		// an unstaged one has since rewritten draws nothing, and so is not listed. A staged
		// deletion sharing its boundary with an unstaged one is listed but never found, since
		// only the unstaged anchor is drawn there.
		std::vector<ChangeHunk> Hunks;
	};

	// The two texts the old lines are read from. Either may be empty, which reads as a text with
	// no lines: an untracked file has no index side, and a new file no HEAD side.
	struct OldTexts
	{
		std::string_view Head;
		std::string_view Index;
	};

	// What the gutter draws for one buffer.
	//
	// The index→buffer diff is the unstaged change and is drawn in full. The HEAD→index diff is
	// the staged change. It is expressed in index lines, so it is carried into buffer lines
	// through the stretches the unstaged diff left alone. Where an unstaged hunk covers the spot,
	// the staged marker is dropped: the change the reader is still making is the one to show.
	ChangeMarkers BuildChangeMarkers(std::span<const Core::LineHunk> headToIndex,
									 std::span<const Core::LineHunk> indexToBuffer,
									 std::size_t bufferLineCount, const OldTexts& old = {});

	// The hunk whose bar is drawn beside `line`: the one of the shade the line is drawn in, since
	// a staged and an unstaged hunk can both span it. Null when the line has no bar.
	const ChangeHunk* FindLineHunk(const ChangeMarkers& markers, std::size_t line);

	// The hunk behind the deletion marker drawn above `line`. Null when there is none.
	const ChangeHunk* FindDeletionHunk(const ChangeMarkers& markers, std::size_t line);

	// The tooltip's first line: the kind, how many lines, and the shade, plus which version the
	// old lines beneath it come from when there are any. A modification that changed the number
	// of lines says how many replaced how many, since a single count would describe neither side.
	std::string DescribeHunk(const ChangeHunk& hunk);
}

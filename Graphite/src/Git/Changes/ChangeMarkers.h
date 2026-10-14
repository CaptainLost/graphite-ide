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

	struct LineChange
	{
		ChangeKind Kind = ChangeKind::None;
		bool Staged = false;

		friend bool operator==(const LineChange&, const LineChange&) = default;
	};

	// WARNING: Line is the buffer line the marker is drawn above; one past the last line means
	// the removal was at the very end.
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

	inline constexpr std::size_t kShownOldLines = 15;

	struct ChangeHunk
	{
		HunkKind Kind = HunkKind::Added;
		bool Staged = false;

		// WARNING: A staged hunk's span runs from its first marked line to its last. An unstaged
		// insertion in the middle splits the markers without splitting the span, which is why a
		// lookup also matches the shade the line is drawn in. A Deleted hunk has LineCount zero.
		std::size_t FirstLine = 0;
		std::size_t LineCount = 0;

		std::size_t MarkedCount = 0;
		bool Intact = true;

		std::size_t OldCount = 0;
		std::vector<std::string> OldLines;
	};

	struct ChangeMarkers
	{
		std::vector<LineChange> Lines;
		std::vector<DeletionAnchor> Deletions;

		// WARNING: A staged hunk whose every line an unstaged one has since rewritten draws
		// nothing, and so is not listed. A staged deletion sharing its boundary with an unstaged
		// one is listed but never found, since only the unstaged anchor is drawn there.
		std::vector<ChangeHunk> Hunks;
	};

	struct OldTexts
	{
		std::string_view Head;
		std::string_view Index;
	};

	// WARNING: The staged diff is expressed in index lines, so it is carried into buffer lines
	// through the stretches the unstaged diff left alone. Where an unstaged hunk covers the spot,
	// the staged marker is dropped: the change the reader is still making is the one to show.
	ChangeMarkers BuildChangeMarkers(std::span<const Core::LineHunk> headToIndex,
									 std::span<const Core::LineHunk> indexToBuffer,
									 std::size_t bufferLineCount, const OldTexts& oldTexts = {});

	const ChangeHunk* FindLineHunk(const ChangeMarkers& markers, std::size_t line);

	const ChangeHunk* FindDeletionHunk(const ChangeMarkers& markers, std::size_t line);

	std::string DescribeHunk(const ChangeHunk& hunk);
}

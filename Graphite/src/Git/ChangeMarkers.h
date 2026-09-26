#pragma once

#include "Core/Git/TextDiff.h"

#include <cstddef>
#include <cstdint>
#include <span>
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

	struct ChangeMarkers
	{
		// Exactly one entry per buffer line.
		std::vector<LineChange> Lines;

		// Ascending by Line, and never two on one line.
		std::vector<DeletionAnchor> Deletions;
	};

	// What the gutter draws for one buffer.
	//
	// The index→buffer diff is the unstaged change and is drawn in full. The HEAD→index diff is
	// the staged change. It is expressed in index lines, so it is carried into buffer lines
	// through the stretches the unstaged diff left alone. Where an unstaged hunk covers the spot,
	// the staged marker is dropped: the change the reader is still making is the one to show.
	ChangeMarkers BuildChangeMarkers(std::span<const Core::LineHunk> headToIndex,
									 std::span<const Core::LineHunk> indexToBuffer,
									 std::size_t bufferLineCount);
}

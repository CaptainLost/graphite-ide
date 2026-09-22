#pragma once

#include "Core/Git/Status.h"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <vector>

namespace Graphite {

	// Which entries of a WorkingTreeStatus belong under which heading, as indices into that
	// status rather than copies of it. Indices because the panel needs the whole FileStatus
	// to draw a row, and copying every entry into three vectors to answer a question about
	// grouping would be a second copy of the same data.
	//
	// A file can be in Staged and Unstaged at once — staged one way and edited again since
	// — which is the case that makes this worth a function rather than a filter written
	// inline three times.
	struct ChangeSections
	{
		std::vector<std::size_t> Conflicted;
		std::vector<std::size_t> Staged;
		std::vector<std::size_t> Unstaged;
	};

	// A conflicted entry goes under Conflicted and nowhere else: what its index and
	// work-tree flags say is not a description of two ordinary halves, and showing it three
	// times would suggest three separate things to act on.
	ChangeSections PartitionStatus(const Core::WorkingTreeStatus& status);

	// The single letter a row is marked with. One letter and not several: a row shows the
	// one fact a reader cannot get from the path, and a file that is both renamed and
	// modified is, to someone scanning a list, a rename.
	char ChangeLetter(Core::StatusFlags flags, bool untracked);

	// Where a selection lands after the status changed under it. `before` and `after` are
	// the paths one section drew last frame and this frame, in drawn order.
	//
	// Kept by path rather than by index because the status is rebuilt on every refresh and
	// its entries reorder; an index would quietly move the selection onto a different file
	// every time a save landed. When the selected path is gone, the row that took its
	// position is selected, so acting on a run of rows leaves the selection where the hand
	// already is. Nothing is returned when the section emptied, or when the selection was
	// not in it to begin with.
	std::optional<std::filesystem::path> NextSelection(
		const std::filesystem::path& selected,
		const std::vector<std::filesystem::path>& before,
		const std::vector<std::filesystem::path>& after);
}

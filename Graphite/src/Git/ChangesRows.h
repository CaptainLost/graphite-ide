#pragma once

#include "Core/Git/Status.h"

#include <cstddef>
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
}

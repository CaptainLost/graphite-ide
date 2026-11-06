#pragma once

#include "Git/Lines/LineReader.h"

#include "Core/Git/Lines/LineModel.h"

#include <cstddef>
#include <optional>
#include <set>
#include <vector>

namespace Graphite {

	// Not merged by history, compared with a line, and with commits of its own: only the commits' contents can tell
	// whether the line holds its changes.
	bool IsContentCandidate(const Core::BranchLine& line);

	// Indices into read.Model.Branches of every candidate, in the order they are checked: the shown branch, the rows in
	// view, the user's own branches, then remote branches, the newest tip first within each. `inView` and `shown` index
	// read.Model.Branches too.
	std::vector<std::size_t> OrderCandidates(const LineRead& read, const std::set<std::size_t>& inView,
											 std::optional<std::size_t> shown);
}

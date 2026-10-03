#pragma once

#include "Core/Git/Oid.h"
#include "Core/Git/Ref.h"

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

namespace Graphite {

	enum class CommitRefKind
	{
		// The branch that is checked out.
		Head,
		Local,
		Remote
	};

	// One branch name worn by a commit in the history list.
	struct CommitRef
	{
		std::string Name;
		CommitRefKind Kind = CommitRefKind::Local;
	};

	// The names that point at each commit, keyed by the commit they point at. A commit no
	// branch tip sits on has no entry. Tags are not here: the model does not list them.
	using CommitRefMap = std::unordered_map<Core::Oid, std::vector<CommitRef>>;

	// Per commit the checked-out branch comes first, then the local branches, then the remote
	// ones, each group by name - so the label the reader is most likely to look for is the
	// one a narrow cell keeps.
	CommitRefMap BuildCommitRefs(const std::vector<Core::Branch>& branches);

	// How many of `widths`, taken in order, fit in `available` with `gap` between neighbours.
	// Labels are cut from the end, never from the middle, so the count is a prefix.
	std::size_t RefLabelsThatFit(const std::vector<float>& widths, float gap, float available);
}

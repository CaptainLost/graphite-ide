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
		Head,
		Local,
		Remote
	};

	struct CommitRef
	{
		std::string Name;
		CommitRefKind Kind = CommitRefKind::Local;
	};

	// WARNING: tags are not in this map because the model does not list them.
	using CommitRefMap = std::unordered_map<Core::Oid, std::vector<CommitRef>>;

	CommitRefMap BuildCommitRefs(const std::vector<Core::Branch>& branches);

	std::size_t RefLabelsThatFit(const std::vector<float>& labelWidths, float gap,
								 float availableWidth, float summaryWidth,
								 float minimumSummaryWidth);
}

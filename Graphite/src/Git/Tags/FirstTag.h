#pragma once

#include "Git/Lines/LineReader.h"

#include "Core/Base/Base.h"

#include "Core/Git/Lines/AncestryGraph.h"
#include "Core/Git/Oid.h"
#include "Core/Git/Tag.h"

#include <cstdint>
#include <vector>

namespace Graphite {

	// Per commit of the graph, the index into `tagsNewestFirst` of the oldest tag whose commit reaches it, or
	// Core::kNoTag: the oldest by When and, of two made in the same second, the one whose name comes first, as git's
	// creatordate order with its name tie-break gives them. A tag whose commit the graph lacks is passed over.
	// WARNING: `tagsNewestFirst` is in Repository::QueryTags' order, newest first and by name within one date.
	std::vector<std::uint32_t> FirstTagsIn(const Core::AncestryGraph& graph, const std::vector<Core::Tag>& tagsNewestFirst);

	// The oldest tag holding the commit, from the read's first-tag answer. Null when no tag holds it and when the
	// commit is not in the read's graph: every listed tag's history is in that graph, so no tag holds such a commit.
	const Core::Tag* FirstTagOf(const LineRead& read, const Core::Oid& commit);

	// Whether the repository has tags, by the newest listing known: `listed`, GitModel's, once it has been read,
	// otherwise the one the read used. False while neither is known.
	bool RepositoryHasTags(const LineRead* read, const Core::Ref<const std::vector<Core::Tag>>& listed);
}

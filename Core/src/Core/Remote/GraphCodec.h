#pragma once

#include "Core/Base/Result.h"
#include "Core/Git/Lines/AncestryGraph.h"

#include <string>
#include <string_view>
#include <vector>

// The commits of a WSL repository's lines graph as they cross the agent's protocol (ADR 0695):
// binary, since a first read of a huge repository is a million commits. A u32 count, then per
// commit its id (20 bytes), flags (bit 0 ParentsCut, bit 1 CommittedByAuthor), time (i64), parent
// count (u32), parents (20 bytes each) and author key (u32 length, bytes); integers little endian.
namespace Core {

	std::string EncodeGraphCommits(const std::vector<GraphCommit>& commits);

	// InvalidArgument when the bytes end before the commits they announce.
	Result<std::vector<GraphCommit>> DecodeGraphCommits(std::string_view bytes);
}

#pragma once

#include "Core/Base/Result.h"
#include "Core/Git/Lines/AncestryGraph.h"

#include <string>
#include <string_view>
#include <vector>

// The commits of a WSL repository's lines graph as they cross the agent's protocol (ADR 0695):
// binary, since a first read of a huge repository is millions of commits. A u32 count of author
// keys, each a u32 length and its bytes; then a u32 commit count and per commit its id (20 bytes),
// flags (bit 0 ParentsCut, bit 1 CommittedByAuthor), time (i64), author key index (u32), parent
// count (u32) and parents (20 bytes each); integers little endian. About 57 bytes for a commit with
// one parent, so the 256 MiB payload holds some 4.5 million.
namespace Core {

	std::string EncodeGraphCommits(const std::vector<GraphCommit>& commits);

	// InvalidArgument when the bytes end before the commits they announce, name a key they do not
	// hold, or go on past them.
	Result<std::vector<GraphCommit>> DecodeGraphCommits(std::string_view bytes);

	// Commit ids one after another, 20 bytes each: a long list kept out of a request's header,
	// which the agent takes up to 1 MiB of.
	std::string EncodeCommitIds(const std::vector<Oid>& ids);
	Result<std::vector<Oid>> DecodeCommitIds(std::string_view bytes);
}

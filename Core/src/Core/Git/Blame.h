#pragma once

#include "Core/Git/Commit.h"
#include "Core/Git/Oid.h"

#include <cstddef>
#include <filesystem>
#include <unordered_map>
#include <vector>

namespace Core {

	struct BlameRequest
	{
		std::filesystem::path File;   // relative to the working directory
		Oid At;                       // the commit to blame at; callers pass HEAD's id
	};

	struct BlameHunk
	{
		std::size_t FirstLine = 1;            // one-based, in the file at BlameRequest::At
		std::size_t LineCount = 0;
		Oid Commit;                           // the commit that last changed these lines
		std::filesystem::path OriginalPath;   // the file's path in Commit
		std::size_t OriginalFirstLine = 1;    // one-based, in Commit's version of the file
	};

	struct Blame
	{
		Oid At;
		std::filesystem::path File;
		std::size_t LineCount = 0;
		std::vector<BlameHunk> Hunks;                     // ascending, contiguous, covering 1..LineCount
		std::unordered_map<Oid, CommitSummary> Commits;   // one per distinct BlameHunk::Commit; Author is mailmapped
	};

	// Null when line is 0 or past LineCount.
	const BlameHunk* HunkOfLine(const Blame& blame, std::size_t line);
}

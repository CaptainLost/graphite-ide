#pragma once

#include "Core/Git/Commit.h"
#include "Core/Git/Diff.h"
#include "Core/Git/Oid.h"
#include "Core/Git/Requests.h"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <vector>

namespace Core {

	struct LineHistoryRequest
	{
		Oid From;                          // the commit that last changed the line
		std::filesystem::path File;        // the file's path in From
		std::size_t Line = 1;              // one-based, in From's version
		std::size_t MaxWalk = kLogWalkLimit;

		friend bool operator==(const LineHistoryRequest&, const LineHistoryRequest&) = default;
	};

	struct LineChange
	{
		CommitSummary Commit;
		std::filesystem::path Path;              // the file's path in Commit
		std::size_t Line = 1;                    // one-based, in Commit's version
		std::optional<std::size_t> ParentLine;   // one-based, in the first parent; empty when Commit added the line
		std::vector<Hunk> Hunks;                 // the hunks of Commit's diff of the file that contain Line
	};

	struct LineHistory
	{
		std::vector<LineChange> Changes;   // newest first
		bool Complete = true;              // false only when MaxWalk or an unreadable diff stopped the walk
	};
}

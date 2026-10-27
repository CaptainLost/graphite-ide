#pragma once

#include "Core/Git/Commit.h"
#include "Core/Git/Diff.h"

#include <chrono>
#include <cstddef>
#include <string>

namespace Graphite {

	struct DiffStats
	{
		std::size_t Files = 0;
		std::size_t Added = 0;
		std::size_t Removed = 0;
	};

	DiffStats CountChanges(const Core::Diff& diff);

	std::string UncountedChangesText(bool diffFailed);

	// WARNING: The offset is the reader's, not the author's, because Core::Signature drops the author's. Seconds are dropped, not rounded.
	std::string FormattedTimestamp(std::chrono::sys_seconds instant, std::chrono::minutes offset);

	std::string CommitMessageText(const Core::CommitSummary& commit);

	bool ShowsCommitter(const Core::CommitSummary& commit);
}

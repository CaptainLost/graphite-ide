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

	// In the zone's offset at the instant itself, not today's: a summer time read in winter keeps its summer offset. A
	// null zone reads as UTC.
	std::string FormattedTimestamp(std::chrono::sys_seconds instant, const std::chrono::time_zone* zone);

	std::string CommitMessageText(const Core::CommitSummary& commit);

	bool ShowsCommitter(const Core::CommitSummary& commit);
}

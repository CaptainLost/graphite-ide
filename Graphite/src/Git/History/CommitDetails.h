#pragma once

#include "Git/Lines/LineReader.h"

#include "Core/Git/Commit.h"
#include "Core/Git/Diff.h"

#include <chrono>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace Graphite {

	struct DiffStats
	{
		std::size_t Files = 0;
		std::size_t Added = 0;
		std::size_t Removed = 0;
	};

	DiffStats CountChanges(const Core::Diff& diff);

	// The stats line while the commit's diff is not in hand; a failure is said with its message, here rather than in the
	// window bar, so it stays with the commit it is about.
	std::string UncountedChangesText(bool diffFailed, std::string_view failure);

	// WARNING: The offset is the reader's, not the author's, because Core::Signature drops the author's. Seconds are dropped, not rounded.
	std::string FormattedTimestamp(std::chrono::sys_seconds instant, std::chrono::minutes offset);

	// In the zone's offset at the instant itself, not today's: a summer time read in winter keeps its summer offset. A
	// null zone reads as UTC.
	std::string FormattedTimestamp(std::chrono::sys_seconds instant, const std::chrono::time_zone* zone);

	std::string CommitMessageText(const Core::CommitSummary& commit);

	bool ShowsCommitter(const Core::CommitSummary& commit);

	// The bases that hold a commit, as the "In bases" line names them.
	struct InBases
	{
		std::vector<std::string> Holding; // short names in model order; a remote base keeps its remote
		bool Pending = false;             // the answer has not landed
		std::optional<std::string> Unknown; // the line read failed: its message, and no answer will come
	};

	// The line for a moment with no read: waiting while one is coming, unknown once it has failed or once the branches
	// it would be made of could not be listed. A failure stays said while a read is asked again after it.
	InBases InBasesWithoutRead(bool reading, const std::optional<Core::Error>& linesError,
							   const std::optional<Core::Error>& branchesError);

	// The line for a commit the read's graph does not hold, as a detached HEAD's own commit or a stash: in no base, since
	// a base's history is in the graph; waiting while a newer read may hold it, but not for one asked again after a failure.
	InBases InBasesOutsideRead(bool reading, const std::optional<Core::Error>& linesError);

	// How many of the "In bases" chips go on the line, which never wraps: as many, in order, as fit in `room` with `gap`
	// between them and, when some are left over, the words for the rest after them, at most `moreWidth` wide.
	std::size_t InBasesChipsThatFit(const std::vector<float>& chipWidths, float gap, float room, float moreWidth);

	// What follows the chips shown: "and 29 more", "30 bases" when none is shown, empty when all are.
	std::string InBasesMoreText(std::size_t shown, std::size_t total);

	// `holding` is LinesModel::BasesHolding: one flag per base of the read, absent while it is asked.
	InBases DescribeInBases(const LineRead& read, const std::optional<std::vector<bool>>& holding);
}

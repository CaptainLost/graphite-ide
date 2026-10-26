#pragma once

#include "Git/History/HistoryReach.h"

#include "Core/Git/Commit.h"
#include "Core/Git/Oid.h"
#include "Core/Git/Requests.h"

#include <cstdint>
#include <filesystem>
#include <string>

namespace Graphite {

	// A commit the history is asked to show, opened at a file under the path it had in that commit.
	struct CommitReveal
	{
		Core::Oid Commit;
		std::filesystem::path Path;
	};

	enum class RevealStep : std::uint8_t
	{
		Select,
		LoadMore,
		FilterByHash
	};

	// What a reveal does with the page HEAD's whole history answered `asked` with: select the commit
	// once the page holds it; load more while the walk stopped at its reach and within the walk bound;
	// otherwise look the hash up.
	RevealStep NextRevealStep(const CommitReveal& reveal, const Core::LogPage& page, const HistoryReach& asked);

	// Whether a page asked with `request` can carry a reveal on: HEAD's whole history, with no ref, no
	// stash and nothing narrowing it. Any other page means the reader picked a branch, a stash or a
	// filter while the commit was being looked for, and that choice wins.
	bool ShowsWholeHeadHistory(const Core::LogRequest& request);

	// The path box's text for a path relative to the git working directory. A filter with a separator
	// is matched against the whole path with forward slashes, so a backslash would match nothing.
	std::string HistoryPathText(const std::filesystem::path& relativePath);
}

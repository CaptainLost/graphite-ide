#include "Git/History/CommitDetails.h"

#include "Git/Compare/VsLabel.h"

#include "Core/Time/TimeZone.h"

#include <algorithm>
#include <format>

namespace Graphite {

	DiffStats CountChanges(const Core::Diff& diff)
	{
		DiffStats changeCounts;
		changeCounts.Files = diff.Files.size();

		for (const Core::FileDiff& fileDiff : diff.Files)
		{
			const Core::LineCounts lines = Core::CountLines(fileDiff);

			changeCounts.Added += lines.Added;
			changeCounts.Removed += lines.Removed;
		}

		return changeCounts;
	}

	std::string UncountedChangesText(bool diffFailed)
	{
		return diffFailed ? "couldn't count changes" : "counting changes\xE2\x80\xA6";
	}

	std::string FormattedTimestamp(std::chrono::sys_seconds instant, std::chrono::minutes offset)
	{
		return std::format("{:%Y-%m-%d %H:%M}", instant + offset);
	}

	std::string FormattedTimestamp(std::chrono::sys_seconds instant, const std::chrono::time_zone* zone)
	{
		return FormattedTimestamp(instant, Core::OffsetAt(zone, instant));
	}

	std::string CommitMessageText(const Core::CommitSummary& commit)
	{
		return commit.Body.empty() ? commit.Summary : commit.Summary + "\n\n" + commit.Body;
	}

	bool ShowsCommitter(const Core::CommitSummary& commit)
	{
		return commit.Committer.Name != commit.Author.Name ||
			   commit.Committer.Email != commit.Author.Email;
	}

	InBases InBasesWithoutRead(bool reading, const std::optional<Core::Error>& linesError,
							   const std::optional<Core::Error>& branchesError)
	{
		InBases bases;

		if (reading)
		{
			bases.Pending = true;
		}
		else if (linesError.has_value())
		{
			bases.Unknown = linesError->Message;
		}
		else if (branchesError.has_value())
		{
			bases.Unknown = branchesError->Message;
		}
		else
		{
			bases.Pending = true;
		}

		return bases;
	}

	InBases InBasesOutsideRead(bool reading)
	{
		InBases bases;
		bases.Pending = reading;

		return bases;
	}

	InBases DescribeInBases(const LineRead& read, const std::optional<std::vector<bool>>& holding)
	{
		InBases bases;

		if (!holding.has_value())
		{
			bases.Pending = true;
			return bases;
		}

		const std::size_t known = std::min(holding->size(), read.Model.Bases.size());

		for (std::size_t index = 0; index < known; ++index)
		{
			if ((*holding)[index])
			{
				bases.Holding.push_back(LineNameOf(read, read.Model.Bases[index].Ref));
			}
		}

		return bases;
	}
}

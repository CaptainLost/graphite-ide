#include "Git/History/CommitDetails.h"

#include "Core/Time/TimeZone.h"

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
}

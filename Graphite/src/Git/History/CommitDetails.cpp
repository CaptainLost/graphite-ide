#include "Git/History/CommitDetails.h"

#include <format>

namespace Graphite {

	DiffStats CountChanges(const Core::Diff& diff)
	{
		DiffStats changeCounts;
		changeCounts.Files = diff.Files.size();

		for (const Core::FileDiff& fileDiff : diff.Files)
		{
			for (const Core::Hunk& hunk : fileDiff.Hunks)
			{
				for (const Core::DiffLine& diffLine : hunk.Lines)
				{
					switch (diffLine.Kind)
					{
					case Core::DiffLineKind::Added:
						++changeCounts.Added;
						break;

					case Core::DiffLineKind::Removed:
						++changeCounts.Removed;
						break;

					case Core::DiffLineKind::Context:
						break;
					}
				}
			}
		}

		return changeCounts;
	}

	std::string FormattedTimestamp(std::chrono::sys_seconds instant, std::chrono::minutes offset)
	{
		return std::format("{:%Y-%m-%d %H:%M}", instant + offset);
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

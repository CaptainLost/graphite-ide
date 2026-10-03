#include "Git/History/CommitDetails.h"

#include <format>

namespace Graphite {

	DiffStats CountChanges(const Core::Diff& diff)
	{
		DiffStats stats;
		stats.Files = diff.Files.size();

		for (const Core::FileDiff& file : diff.Files)
		{
			for (const Core::Hunk& hunk : file.Hunks)
			{
				for (const Core::DiffLine& line : hunk.Lines)
				{
					switch (line.Kind)
					{
					case Core::DiffLineKind::Added:
						++stats.Added;
						break;

					case Core::DiffLineKind::Removed:
						++stats.Removed;
						break;

					case Core::DiffLineKind::Context:
						break;
					}
				}
			}
		}

		return stats;
	}

	std::string FormattedTimestamp(std::chrono::sys_seconds when, std::chrono::minutes offset)
	{
		// Shifted first and then named as though it were UTC, which is what naming an
		// instant in an offset means. The format is fixed rather than the locale's, so a
		// date in a log reads the same on every machine that opens the repository.
		return std::format("{:%Y-%m-%d %H:%M}", when + offset);
	}

	bool ShowsCommitter(const Core::CommitSummary& commit)
	{
		return commit.Committer.Name != commit.Author.Name ||
			   commit.Committer.Email != commit.Author.Email;
	}
}

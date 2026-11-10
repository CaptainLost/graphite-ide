#include "Git/Lines/LineSource.h"

namespace Graphite {

	Core::Result<std::vector<bool>> RepositoryLineSource::HoldsCommits(const std::vector<Core::Oid>& commits)
	{
		std::vector<bool> held;
		held.reserve(commits.size());

		for (const Core::Oid& commit : commits)
		{
			const Core::Result<bool> holds = m_Repository.HoldsCommit(commit);

			if (!holds)
			{
				return holds.GetError();
			}

			held.push_back(holds.GetValue());
		}

		return held;
	}
}

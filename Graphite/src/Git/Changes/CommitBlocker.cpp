#include "Git/Changes/CommitBlocker.h"

namespace Graphite {

	std::optional<std::string_view> WhyCommitIsDisabled(const CommitBlockerInputs& inputs)
	{
		if (inputs.WriteInFlight)
		{
			return "A write is in progress";
		}

		if (inputs.HeadUnread)
		{
			return "Reading where HEAD is";
		}

		if (inputs.Amend)
		{
			return std::nullopt;
		}

		if (!inputs.AnythingStaged && !inputs.Merging)
		{
			return "Stage changes to commit";
		}

		if (inputs.MessageIsEmpty)
		{
			return "Type a commit message";
		}

		return std::nullopt;
	}
}

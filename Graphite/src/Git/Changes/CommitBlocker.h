#pragma once

#include <optional>
#include <string_view>

namespace Graphite {

	struct CommitBlockerInputs
	{
		bool WriteInFlight = false;

		// The host's writes expect the HEAD the user saw, and none has been read yet.
		bool HeadUnread = false;

		bool AnythingStaged = false;
		bool Merging = false;

		bool MessageIsEmpty = true;
		bool Amend = false;
	};

	std::optional<std::string_view> WhyCommitIsDisabled(const CommitBlockerInputs& inputs);
}

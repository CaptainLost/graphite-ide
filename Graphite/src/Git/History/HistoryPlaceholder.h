#pragma once

#include "Core/Base/Result.h"

#include <optional>
#include <string>

namespace Graphite {

	bool IsMissingRepository(bool hasStatus, const std::optional<Core::Error>& statusError);

	enum class LogAttempt
	{
		Settled,
		InFlight
	};

	// Nothing while the history is still loading, so the caller shows a loader rather than a message.
	std::optional<std::string> HistoryPlaceholder(bool hasStatus, const std::optional<Core::Error>& statusError,
												  const std::optional<Core::Error>& logError,
												  LogAttempt attempt = LogAttempt::Settled);

	// The waiting row of a history held back: the shown branch's content check once nothing of the lines is awaited any
	// more (their read, a held log, a restore), the lines otherwise.
	std::string HeldHistoryText(bool linesAwaited, bool contentChecked);
}

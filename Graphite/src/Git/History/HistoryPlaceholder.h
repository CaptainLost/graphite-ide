#pragma once

#include "Core/Base/Result.h"

#include <optional>
#include <string>

namespace Graphite {

	bool IsMissingRepository(bool hasStatus, const std::optional<Core::Error>& statusError);

	// Nothing while the history is still loading, so the caller shows a loader rather than a message.
	std::optional<std::string> HistoryPlaceholder(bool hasStatus, const std::optional<Core::Error>& statusError,
												  const std::optional<Core::Error>& logError);
}

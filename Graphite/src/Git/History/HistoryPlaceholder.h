#pragma once

#include "Core/Base/Result.h"

#include <optional>
#include <string>

namespace Graphite {

	bool IsMissingRepository(bool hasStatus, const std::optional<Core::Error>& statusError);

	std::string HistoryPlaceholder(bool hasStatus, const std::optional<Core::Error>& statusError,
								   const std::optional<Core::Error>& logError);
}

#include "Git/HistoryPlaceholder.h"

namespace Graphite {

	std::string HistoryPlaceholder(bool hasStatus, const std::optional<Core::Error>& statusError,
								   const std::optional<Core::Error>& logError)
	{
		if (!hasStatus && statusError.has_value() && statusError->Code == Core::ErrorCode::NotFound)
		{
			return "No repository.";
		}

		if (logError.has_value())
		{
			return "Could not load the history: " + logError->Message;
		}

		return "Loading history…";
	}
}

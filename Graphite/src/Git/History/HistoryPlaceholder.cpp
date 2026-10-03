#include "Git/History/HistoryPlaceholder.h"

namespace Graphite {

	bool IsMissingRepository(bool hasStatus, const std::optional<Core::Error>& statusError)
	{
		return !hasStatus && statusError.has_value() && statusError->Code == Core::ErrorCode::NotFound;
	}

	std::string HistoryPlaceholder(bool hasStatus, const std::optional<Core::Error>& statusError,
								   const std::optional<Core::Error>& logError)
	{
		if (IsMissingRepository(hasStatus, statusError))
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

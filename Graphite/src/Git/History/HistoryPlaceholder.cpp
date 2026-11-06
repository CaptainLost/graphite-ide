#include "Git/History/HistoryPlaceholder.h"

namespace Graphite {

	bool IsMissingRepository(bool hasStatus, const std::optional<Core::Error>& statusError)
	{
		return !hasStatus && statusError.has_value() && statusError->Code == Core::ErrorCode::NotFound;
	}

	std::optional<std::string> HistoryPlaceholder(bool hasStatus, const std::optional<Core::Error>& statusError,
												  const std::optional<Core::Error>& logError, LogAttempt attempt)
	{
		if (IsMissingRepository(hasStatus, statusError))
		{
			return "No repository.";
		}

		if (logError.has_value() && attempt == LogAttempt::Settled)
		{
			return "Could not load the history: " + logError->Message;
		}

		return std::nullopt;
	}

	std::string HeldHistoryText(bool linesAwaited, bool contentChecked)
	{
		if (!linesAwaited && contentChecked)
		{
			return "Checking whether the branch's changes are in its line\xE2\x80\xA6";
		}

		return "Reading the branches' lines\xE2\x80\xA6";
	}
}

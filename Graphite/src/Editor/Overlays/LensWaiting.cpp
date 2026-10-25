#include "Editor/Overlays/LensWaiting.h"

#include "Language/Server/ServerWaitingText.h"
#include "UI/TypographicMarks.h"

namespace Graphite {

	std::string LensWaitingText(const ServerStatus& status)
	{
		if (HasNoServerToWaitFor(status))
		{
			return {};
		}

		switch (AvailabilityOf(status))
		{
		case ServerAvailability::Answers:
			if (IsStillIndexing(status))
			{
				return ServerWaitingText(status);
			}

			return "counting references" + std::string{kEllipsis};

		case ServerAvailability::ComingUp:
			return ServerWaitingText(status);

		case ServerAvailability::Unavailable:
			break;
		}

		return ServerUnavailableText(status);
	}
}

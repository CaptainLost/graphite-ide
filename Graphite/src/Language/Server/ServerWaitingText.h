#pragma once

#include "Language/Api/LanguageService.h"
#include "Language/Server/LspClient.h"

#include <cstdint>
#include <string>

namespace Graphite {

	enum class ServerAvailability : std::uint8_t
	{
		Answers,
		ComingUp,
		Unavailable
	};

	ServerAvailability AvailabilityOf(const ServerStatus& status);

	bool IsStillIndexing(const ServerStatus& status);

	bool HasNoServerToWaitFor(const ServerStatus& status);

	std::string ServerNameOf(const ServerStatus& status);

	std::string ServerWaitingText(const ServerStatus& status);

	std::string ServerUnavailableText(const ServerStatus& status);

	std::string StillIndexingText(const ServerStatus& status);

	std::string ReplyFailureText(const ServerReply& reply);

	// WARNING: Only the first letter of a phrase that does not open on the server's name, which keeps its own
	// spelling: "clangd stopped" stays as it is.
	std::string AsSentence(const std::string& phrase, const ServerStatus& status);
}

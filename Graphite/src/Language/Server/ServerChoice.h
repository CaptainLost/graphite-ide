#pragma once

#include "Language/Api/LanguageId.h"
#include "Language/Server/ServerCatalog.h"
#include "Language/Server/ServerLaunch.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace Graphite {

	enum class ServerChoiceKind : std::uint8_t
	{
		NotConfigured,
		Off,
		Chosen
	};

	struct ServerChoice
	{
		ServerChoiceKind Kind = ServerChoiceKind::NotConfigured;
		const ServerDefinition* Server = nullptr;
		ServerOverrides Overrides;
	};

	inline constexpr std::string_view kServerTurnedOff = "none";

	ServerChoice ChooseServer(LanguageId language, const std::optional<std::string>& storedChoice);
}

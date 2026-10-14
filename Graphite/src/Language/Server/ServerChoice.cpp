#include "Language/Server/ServerChoice.h"

#include "Language/Registry/LanguageRegistry.h"

namespace Graphite {

	namespace {

		bool LanguageListsServer(LanguageId language, std::string_view serverId)
		{
			for (const std::string_view listed : DefinitionOf(language).Servers)
			{
				if (listed == serverId)
				{
					return true;
				}
			}

			return false;
		}

		ServerChoice RecommendedChoice(LanguageId language)
		{
			return ServerChoice{.Kind = ServerChoiceKind::Chosen, .Server = RecommendedServerFor(language)};
		}
	}

	ServerChoice ChooseServer(LanguageId language, const std::optional<std::string>& storedChoice)
	{
		if (!LanguageHasServers(language))
		{
			return ServerChoice{};
		}

		if (!storedChoice)
		{
			return ServerChoice{};
		}

		if (*storedChoice == kServerTurnedOff)
		{
			return ServerChoice{.Kind = ServerChoiceKind::Off};
		}

		if (!LanguageListsServer(language, *storedChoice))
		{
			return RecommendedChoice(language);
		}

		return ServerChoice{.Kind = ServerChoiceKind::Chosen, .Server = FindServer(*storedChoice)};
	}
}

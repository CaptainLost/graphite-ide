#include "App/LanguageServerChoice.h"

#include "Language/Registry/LanguageRegistry.h"
#include "Settings/AppSettings.h"

#include <nlohmann/json.hpp>

namespace Graphite {

	namespace {

		nlohmann::json ParsedOrNull(const std::string& json)
		{
			if (json.empty())
			{
				return nullptr;
			}

			const nlohmann::json::parser_callback_t noParseCallback = nullptr;
			const bool allowExceptions = false;
			nlohmann::json parsed = nlohmann::json::parse(json, noParseCallback, allowExceptions);

			if (parsed.is_discarded())
			{
				return nullptr;
			}

			return parsed;
		}

		// languageServerBinary predates per-server settings, and a path stored there must still start clangd.
		std::filesystem::path BinaryFor(const AppSettings& settings, const ServerDefinition& server, const LanguageServerSettings& stored)
		{
			const bool isClangd = server.Id == "clangd";

			if (stored.BinaryPath.empty() && isClangd)
			{
				return settings.GetLanguageServerBinary();
			}

			return stored.BinaryPath;
		}
	}

	ServerChoice ReadServerChoice(const AppSettings& settings, LanguageId language)
	{
		ServerChoice choice = ChooseServer(language, settings.GetLanguageServerChoice(DefinitionOf(language).SettingsName));

		if (choice.Kind != ServerChoiceKind::Chosen)
		{
			return choice;
		}

		const LanguageServerSettings stored = settings.GetLanguageServerSettings(choice.Server->Id);

		choice.Overrides = ServerOverrides{.Binary = BinaryFor(settings, *choice.Server, stored),
										   .ExtraArguments = stored.Arguments,
										   .InitializationOptions = ParsedOrNull(stored.InitializationOptionsJson),
										   .Settings = ParsedOrNull(stored.SettingsJson)};

		return choice;
	}
}

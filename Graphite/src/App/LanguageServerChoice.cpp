#include "App/LanguageServerChoice.h"

#include "Language/Registry/LanguageRegistry.h"
#include "Settings/AppSettings.h"
#include "Settings/JsonText.h"

#include <nlohmann/json.hpp>

namespace Graphite {

	ServerChoice ReadServerChoice(const AppSettings& settings, LanguageId language)
	{
		ServerChoice choice = ChooseServer(language, settings.GetLanguageServerChoice(DefinitionOf(language).SettingsName));

		if (choice.Kind != ServerChoiceKind::Chosen)
		{
			return choice;
		}

		const LanguageServerSettings stored = settings.GetLanguageServerSettings(choice.Server->Id);

		choice.Overrides = ServerOverrides{.Binary = stored.BinaryPath,
										   .ExtraArguments = stored.Arguments,
										   .InitializationOptions = ParseJsonText(stored.InitializationOptionsJson),
										   .Settings = ParseJsonText(stored.SettingsJson)};

		return choice;
	}
}

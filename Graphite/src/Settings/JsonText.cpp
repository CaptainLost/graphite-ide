#include "Settings/JsonText.h"

namespace Graphite {

	nlohmann::json ParseJsonText(std::string_view text)
	{
		if (text.empty())
		{
			return nullptr;
		}

		const nlohmann::json::parser_callback_t noParseCallback = nullptr;
		const bool allowExceptions = false;
		nlohmann::json parsed = nlohmann::json::parse(text.begin(), text.end(), noParseCallback, allowExceptions);

		if (parsed.is_discarded())
		{
			return nullptr;
		}

		return parsed;
	}
}

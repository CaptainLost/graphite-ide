#pragma once

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <string_view>

namespace Graphite {

	// Each reader answers a missing key or a value of another type with nothing; nlohmann's own accessors throw.

	inline nlohmann::json ParseJsonText(std::string_view text)
	{
		const nlohmann::json::parser_callback_t noParseCallback = nullptr;
		const bool allowExceptions = false;
		return nlohmann::json::parse(text.begin(), text.end(), noParseCallback, allowExceptions);
	}

	inline std::optional<std::string> TextAt(const nlohmann::json& object, const char* key)
	{
		if (!object.is_object())
		{
			return std::nullopt;
		}

		const nlohmann::json::const_iterator found = object.find(key);

		if (found == object.end() || !found->is_string())
		{
			return std::nullopt;
		}

		return found->get<std::string>();
	}

	inline const nlohmann::json* ArrayAt(const nlohmann::json& object, const char* key)
	{
		if (!object.is_object())
		{
			return nullptr;
		}

		const nlohmann::json::const_iterator found = object.find(key);
		return found != object.end() && found->is_array() ? &*found : nullptr;
	}

	inline const nlohmann::json* ObjectAt(const nlohmann::json& object, const char* key)
	{
		if (!object.is_object())
		{
			return nullptr;
		}

		const nlohmann::json::const_iterator found = object.find(key);
		return found != object.end() && found->is_object() ? &*found : nullptr;
	}

	inline std::optional<bool> FlagAt(const nlohmann::json& object, const char* key)
	{
		if (!object.is_object())
		{
			return std::nullopt;
		}

		const nlohmann::json::const_iterator found = object.find(key);

		if (found == object.end() || !found->is_boolean())
		{
			return std::nullopt;
		}

		return found->get<bool>();
	}
}

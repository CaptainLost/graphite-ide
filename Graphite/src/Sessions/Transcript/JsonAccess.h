#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <string>

namespace Graphite {

	// WARNING: Every accessor here answers a missing field or a wrong type with a default instead of
	// throwing: a field whose type changed in a newer Claude Code must cost that field, not the line.

	inline std::string StringAt(const nlohmann::json& object, const char* key)
	{
		const nlohmann::json::const_iterator field = object.find(key);
		return field != object.end() && field->is_string() ? field->get<std::string>() : std::string{};
	}

	// WARNING: Zero is kept as a count, since a field can genuinely report zero.
	inline std::optional<std::uint64_t> OptionalCountAt(const nlohmann::json& object, const char* key)
	{
		const nlohmann::json::const_iterator field = object.find(key);

		if (field == object.end())
		{
			return std::nullopt;
		}
		if (field->is_number_unsigned())
		{
			return field->get<std::uint64_t>();
		}
		if (field->is_number_integer() && field->get<std::int64_t>() >= 0)
		{
			return static_cast<std::uint64_t>(field->get<std::int64_t>());
		}

		return std::nullopt;
	}

	inline std::uint64_t CountAt(const nlohmann::json& object, const char* key)
	{
		return OptionalCountAt(object, key).value_or(0);
	}

	inline nlohmann::json ObjectOf(const std::string& text)
	{
		constexpr bool kAllowExceptions = false;
		nlohmann::json parsed = nlohmann::json::parse(text, nullptr, kAllowExceptions);
		return parsed.is_object() ? parsed : nlohmann::json::object();
	}

} // namespace Graphite

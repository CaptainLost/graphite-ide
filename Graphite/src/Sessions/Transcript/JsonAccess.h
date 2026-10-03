#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <string>

namespace Graphite {

	// Transcript JSON accessors. Every one answers a missing field or a wrong type with a
	// default instead of throwing: a field whose type changed in a newer Claude Code must
	// cost that field, not the line.

	// The string at `key`, or an empty string.
	inline std::string StringAt(const nlohmann::json& object, const char* key)
	{
		const auto found = object.find(key);
		return found != object.end() && found->is_string() ? found->get<std::string>() : std::string{};
	}

	// A non-negative integer that may be missing: nothing for an absent field, a wrong type or
	// a negative number. Zero is kept, since a field can genuinely report zero.
	inline std::optional<std::uint64_t> OptionalCountAt(const nlohmann::json& object, const char* key)
	{
		const auto found = object.find(key);

		if (found == object.end())
		{
			return std::nullopt;
		}
		if (found->is_number_unsigned())
		{
			return found->get<std::uint64_t>();
		}
		if (found->is_number_integer() && found->get<std::int64_t>() >= 0)
		{
			return static_cast<std::uint64_t>(found->get<std::int64_t>());
		}

		return std::nullopt;
	}

	// The count at `key`, or zero where OptionalCountAt would give nothing. For fields with no
	// meaningful "absent", where a zero already says "none".
	inline std::uint64_t CountAt(const nlohmann::json& object, const char* key)
	{
		return OptionalCountAt(object, key).value_or(0);
	}

	// The JSON object `text` holds, or an empty object when it is not valid JSON or not an object.
	inline nlohmann::json ObjectOf(const std::string& text)
	{
		nlohmann::json parsed = nlohmann::json::parse(text, nullptr, false);
		return parsed.is_object() ? parsed : nlohmann::json::object();
	}

} // namespace Graphite

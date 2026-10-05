#pragma once

#include <charconv>
#include <concepts>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace Core {

	std::string JoinedWith(std::span<const std::string> parts, std::string_view separator);

	// `part` must not view into `text`: appending the separator can move text's characters.
	void AppendSeparated(std::string& text, std::string_view separator, std::string_view part);

	void AppendSeparatedIfNotEmpty(std::string& text, std::string_view separator, std::string_view part);

	std::vector<std::string> SplitSkippingEmpty(std::string_view text, char separator);

	constexpr std::string_view FirstLine(std::string_view text)
	{
		return text.substr(0, text.find('\n'));
	}

	// The whole text as a number, or nothing: a trailing letter, an empty string and, for an
	// unsigned target, a minus sign are all absent, never a wrapped or partial value.
	template<typename Number>
		requires std::integral<Number> || std::floating_point<Number>
	std::optional<Number> ParseNumber(std::string_view text)
	{
		if (text.empty())
		{
			return std::nullopt;
		}

		Number value{};
		const char* last = text.data() + text.size();
		const auto [end, error] = std::from_chars(text.data(), last, value);

		if (error != std::errc{} || end != last)
		{
			return std::nullopt;
		}

		return value;
	}
}

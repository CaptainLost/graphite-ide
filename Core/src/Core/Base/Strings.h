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

	// WARNING: The lines view into `text`, so they last only as long as it does.
	std::vector<std::string_view> SplitLines(std::string_view text);

	// "1 file", "3 files". The plural is given where adding an s would be wrong: "3 matches".
	template<typename Count>
		requires std::integral<Count>
	std::string Counted(Count count, std::string_view singular, std::string_view plural)
	{
		std::string text = std::to_string(count);
		text.push_back(' ');
		text += count == Count{1} ? singular : plural;
		return text;
	}

	template<typename Count>
		requires std::integral<Count>
	std::string Counted(Count count, std::string_view singular)
	{
		return Counted(count, singular, std::string{singular} + 's');
	}

	constexpr std::string_view FirstLine(std::string_view text)
	{
		return text.substr(0, text.find('\n'));
	}

	template<typename Number>
		requires std::integral<Number> || std::floating_point<Number>
	std::optional<Number> ParseNumber(std::string_view text)
	{
		if (text.empty())
		{
			return std::nullopt;
		}

		Number parsedValue{};
		const char* textEnd = text.data() + text.size();
		const auto [parseEnd, parseError] = std::from_chars(text.data(), textEnd, parsedValue);

		const bool parseFailed = parseError != std::errc{};
		const bool charactersLeftUnread = parseEnd != textEnd;
		if (parseFailed || charactersLeftUnread)
		{
			return std::nullopt;
		}

		return parsedValue;
	}
}

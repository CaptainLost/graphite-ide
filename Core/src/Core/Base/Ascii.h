#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace Core {

	// ASCII only, on purpose: callers compare bytes (extensions, header names, ids, search text),
	// and a locale's case fold would promise a correctness across scripts that bytes cannot keep.
	constexpr char LowerAscii(char character)
	{
		const bool isAsciiCapital = character >= 'A' && character <= 'Z';

		if (isAsciiCapital)
		{
			return static_cast<char>(character - 'A' + 'a');
		}

		return character;
	}

	constexpr bool EqualsIgnoringCase(std::string_view left, std::string_view right)
	{
		if (left.size() != right.size())
		{
			return false;
		}

		for (std::size_t characterIndex = 0; characterIndex < left.size(); ++characterIndex)
		{
			if (LowerAscii(left[characterIndex]) != LowerAscii(right[characterIndex]))
			{
				return false;
			}
		}

		return true;
	}

	constexpr bool EqualsIgnoringCase(char left, char right)
	{
		return LowerAscii(left) == LowerAscii(right);
	}

	// The six bytes the C locale calls space, without asking the locale.
	constexpr bool IsAsciiWhitespace(char character)
	{
		constexpr std::string_view kAsciiWhitespace = " \t\n\v\f\r";

		return kAsciiWhitespace.find(character) != std::string_view::npos;
	}

	constexpr std::optional<std::uint8_t> HexDigitValue(char character)
	{
		constexpr int kValueOfDigitA = 10;

		if (character >= '0' && character <= '9')
		{
			return static_cast<std::uint8_t>(character - '0');
		}

		if (character >= 'a' && character <= 'f')
		{
			return static_cast<std::uint8_t>(character - 'a' + kValueOfDigitA);
		}

		if (character >= 'A' && character <= 'F')
		{
			return static_cast<std::uint8_t>(character - 'A' + kValueOfDigitA);
		}

		return std::nullopt;
	}

	inline constexpr std::string_view kLowercaseHexDigits = "0123456789abcdef";

	constexpr std::string_view WithoutLeading(std::string_view text, std::string_view characters)
	{
		const std::size_t firstKept = text.find_first_not_of(characters);

		return text.substr(firstKept == std::string_view::npos ? text.size() : firstKept);
	}

	constexpr std::string_view WithoutTrailing(std::string_view text, std::string_view characters)
	{
		const std::size_t lastKept = text.find_last_not_of(characters);

		return text.substr(0, lastKept == std::string_view::npos ? 0 : lastKept + 1);
	}

	constexpr std::string_view WithoutSurrounding(std::string_view text, std::string_view characters)
	{
		return WithoutTrailing(WithoutLeading(text, characters), characters);
	}

	constexpr bool ContainsOnlySpacesAndTabs(std::string_view text)
	{
		return text.find_first_not_of(" \t") == std::string_view::npos;
	}

	std::string Lowered(std::string_view text);
	std::string Trimmed(std::string_view text);

	bool LoweredTextContains(std::string_view text, std::string_view loweredNeedle);
}
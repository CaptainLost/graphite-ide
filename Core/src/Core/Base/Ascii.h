#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace Core {

	// ASCII only, deliberately: every caller compares bytes - extensions, header names, ids,
	// search text - and a locale's fold would promise a correctness across scripts that a byte
	// comparison cannot keep. Every other byte comes back as it was.
	constexpr char LowerAscii(char character)
	{
		return character >= 'A' && character <= 'Z' ? static_cast<char>(character - 'A' + 'a') : character;
	}

	// Whole-string equality through LowerAscii.
	constexpr bool EqualsIgnoringCase(std::string_view left, std::string_view right)
	{
		if (left.size() != right.size())
		{
			return false;
		}

		for (std::size_t index = 0; index < left.size(); ++index)
		{
			if (LowerAscii(left[index]) != LowerAscii(right[index]))
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
		return character == ' ' || character == '\t' || character == '\n' || character == '\v' || character == '\f' ||
			   character == '\r';
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

	// Index it with 0 to 15 only: [16] is out of range for a string_view.
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

	// The needle must already be ASCII-lowered: an upper-case needle never matches.
	bool LoweredTextContains(std::string_view text, std::string_view loweredNeedle);
}
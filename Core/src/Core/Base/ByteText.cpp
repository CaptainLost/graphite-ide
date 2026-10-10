#include "corepch.h"

#include "Core/Base/ByteText.h"

#include "Core/Base/Ascii.h"

namespace Core {

	namespace {

		constexpr std::string_view kBase64Alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
		constexpr int kBitsPerBase64Digit = 6;
		constexpr int kBitsPerByte = 8;

		std::optional<std::uint8_t> Base64DigitValue(char digit)
		{
			const std::size_t position = kBase64Alphabet.find(digit);

			if (position == std::string_view::npos)
			{
				return std::nullopt;
			}

			return static_cast<std::uint8_t>(position);
		}

		std::size_t PaddingOf(std::string_view text)
		{
			if (text.ends_with("=="))
			{
				return 2;
			}

			return text.ends_with('=') ? 1 : 0;
		}
	}

	std::string BytesToHex(std::span<const std::uint8_t> bytes)
	{
		std::string hex;
		hex.reserve(bytes.size() * 2);

		for (const std::uint8_t byte : bytes)
		{
			hex.push_back(kLowercaseHexDigits[byte >> 4]);
			hex.push_back(kLowercaseHexDigits[byte & 0x0F]);
		}

		return hex;
	}

	std::optional<std::vector<std::uint8_t>> BytesFromHex(std::string_view text)
	{
		if (text.size() % 2 != 0)
		{
			return std::nullopt;
		}

		std::vector<std::uint8_t> bytes;
		bytes.reserve(text.size() / 2);

		for (std::size_t pairStart = 0; pairStart < text.size(); pairStart += 2)
		{
			const std::optional<std::uint8_t> high = HexDigitValue(text[pairStart]);
			const std::optional<std::uint8_t> low = HexDigitValue(text[pairStart + 1]);

			if (!high || !low)
			{
				return std::nullopt;
			}

			bytes.push_back(static_cast<std::uint8_t>((*high << 4) | *low));
		}

		return bytes;
	}

	std::optional<std::vector<std::uint8_t>> BytesFromBase64(std::string_view text)
	{
		if (text.size() % 4 != 0)
		{
			return std::nullopt;
		}

		const std::string_view digits = text.substr(0, text.size() - PaddingOf(text));
		std::vector<std::uint8_t> bytes;
		bytes.reserve(text.size() / 4 * 3);

		// Only the bits not yet emitted matter; the mask keeps the shift from carrying older ones.
		std::uint32_t pendingBits = 0;
		int pendingBitCount = 0;

		for (const char digit : digits)
		{
			const std::optional<std::uint8_t> value = Base64DigitValue(digit);

			if (!value)
			{
				return std::nullopt;
			}

			pendingBits = ((pendingBits << kBitsPerBase64Digit) | *value) & 0xFFFF;
			pendingBitCount += kBitsPerBase64Digit;

			if (pendingBitCount >= kBitsPerByte)
			{
				pendingBitCount -= kBitsPerByte;
				bytes.push_back(static_cast<std::uint8_t>((pendingBits >> pendingBitCount) & 0xFF));
			}
		}

		return bytes;
	}
}

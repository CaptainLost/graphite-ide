#include "corepch.h"

#include "Core/Git/Oid.h"

#include "Core/Base/Ascii.h"
#include "Core/Base/ByteText.h"

#include <algorithm>

namespace Core {

	namespace {
		constexpr std::size_t kHexDigitsPerByte = 2;
		constexpr int kBitsPerHexDigit = 4;
		constexpr std::uint8_t kLowHexDigitMask = 0x0F;
		constexpr int kBitsPerByte = 8;
	}

	std::optional<Oid> Oid::FromHex(std::string_view hex)
	{
		const std::optional<std::vector<std::uint8_t>> bytes = BytesFromHex(hex);
		Oid id;

		if (!bytes.has_value() || bytes->size() != id.Bytes.size())
		{
			return std::nullopt;
		}

		std::copy(bytes->begin(), bytes->end(), id.Bytes.begin());
		return id;
	}

	bool Oid::IsZero() const
	{
		for (const std::uint8_t value : Bytes)
		{
			if (value != 0)
			{
				return false;
			}
		}

		return true;
	}

	std::string Oid::ToHex() const
	{
		std::string text;
		text.reserve(Bytes.size() * kHexDigitsPerByte);

		for (const std::uint8_t value : Bytes)
		{
			text.push_back(kLowercaseHexDigits[value >> kBitsPerHexDigit]);
			text.push_back(kLowercaseHexDigits[value & kLowHexDigitMask]);
		}

		return text;
	}

	std::string Oid::ShortHex(std::size_t digits) const
	{
		const std::size_t firstDigit = 0;
		const std::size_t wanted = std::min(digits, Bytes.size() * kHexDigitsPerByte);
		return ToHex().substr(firstDigit, wanted);
	}
}

// A git object name comes out of SHA-1, so its leading bytes are already a good hash and
// mixing them again would buy nothing.
std::size_t std::hash<Core::Oid>::operator()(const Core::Oid& id) const noexcept
{
	std::size_t hashValue = 0;

	for (std::size_t byteIndex = 0; byteIndex < sizeof(std::size_t) && byteIndex < id.Bytes.size(); ++byteIndex)
	{
		hashValue = (hashValue << Core::kBitsPerByte) | id.Bytes[byteIndex];
	}

	return hashValue;
}

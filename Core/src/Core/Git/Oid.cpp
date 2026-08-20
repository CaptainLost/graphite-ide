#include "corepch.h"

#include "Core/Git/Oid.h"

#include <algorithm>

namespace Core {

	namespace {
		constexpr char kDigits[] = "0123456789abcdef";
	}

	bool Oid::IsZero() const
	{
		return std::all_of(Bytes.begin(), Bytes.end(),
						   [](std::uint8_t value) { return value == 0; });
	}

	std::string Oid::ToHex() const
	{
		std::string text;
		text.reserve(Bytes.size() * 2);

		for (std::uint8_t value : Bytes)
		{
			text.push_back(kDigits[value >> 4]);
			text.push_back(kDigits[value & 0x0F]);
		}

		return text;
	}

	std::string Oid::ShortHex(std::size_t digits) const
	{
		const std::size_t wanted = std::min(digits, Bytes.size() * 2);
		return ToHex().substr(0, wanted);
	}
}

std::size_t std::hash<Core::Oid>::operator()(const Core::Oid& id) const noexcept
{
	// The first eight bytes of a git object name are already a good hash: they come out
	// of SHA-1, so mixing them again would buy nothing.
	std::size_t value = 0;

	for (std::size_t index = 0; index < sizeof(std::size_t) && index < id.Bytes.size(); ++index)
	{
		value = (value << 8) | id.Bytes[index];
	}

	return value;
}

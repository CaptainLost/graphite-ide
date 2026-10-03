#include "corepch.h"

#include "Core/Base/Uuid.h"

#include <cstring>
#include <random>

namespace Core {

	Uuid Uuid::Generate()
	{
		// random_device only seeds: a conforming one is allowed to be deterministic, and
		// drawing every bit from it is slow where it is not. thread_local because the engine
		// is stateful and this may be called from the UI thread and a worker.
		static thread_local std::mt19937_64 engine{std::random_device{}()};

		std::uniform_int_distribution<std::uint64_t> distribution;

		// Every byte is random, so neither which half lands where nor the byte order within a
		// half is an ordering there is anything to get wrong about.
		const std::uint64_t halves[2] = {distribution(engine), distribution(engine)};

		static_assert(sizeof(halves) == kByteCount, "two 64-bit draws are one UUID");

		Uuid uuid;
		std::memcpy(uuid.m_Bytes.data(), halves, kByteCount);

		// RFC 4122 section 4.4: version in the top nibble of byte 6, variant in the top two
		// bits of byte 8, leaving the other hundred and twenty-two bits random.
		uuid.m_Bytes[6] = static_cast<std::uint8_t>((uuid.m_Bytes[6] & 0x0F) | 0x40);
		uuid.m_Bytes[8] = static_cast<std::uint8_t>((uuid.m_Bytes[8] & 0x3F) | 0x80);

		return uuid;
	}

	std::string Uuid::ToString() const
	{
		// The 8-4-4-4-12 hexadecimal digits of the canonical form, in bytes.
		constexpr std::size_t kGroupSizes[] = {4, 2, 2, 2, 6};

		std::string text;
		text.reserve(36);

		std::size_t byte = 0;

		for (const std::size_t size : kGroupSizes)
		{
			if (!text.empty())
			{
				text += '-';
			}

			for (std::size_t i = 0; i < size; ++i, ++byte)
			{
				text += std::format("{:02X}", m_Bytes[byte]);
			}
		}

		return text;
	}
}
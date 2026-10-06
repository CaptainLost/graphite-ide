#include "corepch.h"

#include "Core/Base/Uuid.h"

#include <cstring>
#include <iterator>
#include <random>

namespace Core {

	namespace {
		constexpr std::size_t kVersionByteIndex = 6;
		constexpr std::uint8_t kBitsBelowVersionMask = 0x0F;
		constexpr std::uint8_t kRandomVersionInTopNibble = 0x40;

		constexpr std::size_t kVariantByteIndex = 8;
		constexpr std::uint8_t kBitsBelowVariantMask = 0x3F;
		constexpr std::uint8_t kRfc4122VariantInTopTwoBits = 0x80;

		constexpr std::size_t kCanonicalGroupByteCounts[] = {4, 2, 2, 2, 6};
		constexpr std::size_t kHexDigitsPerByte = 2;
		constexpr std::size_t kGroupSeparatorCount = std::size(kCanonicalGroupByteCounts) - 1;
		constexpr std::size_t kCanonicalTextLength = Uuid::kByteCount * kHexDigitsPerByte + kGroupSeparatorCount;
	}

	Uuid Uuid::Generate()
	{
		// random_device only seeds: a conforming one may be deterministic, and drawing every bit
		// from it is slow where it is not. thread_local because the engine is stateful and nothing
		// confines this function to one thread.
		static thread_local std::mt19937_64 engine{std::random_device{}()};

		std::uniform_int_distribution<std::uint64_t> distribution;

		const std::uint64_t randomHalves[2] = {distribution(engine), distribution(engine)};

		static_assert(sizeof(randomHalves) == kByteCount, "two 64-bit draws are one UUID");

		Uuid uuid;
		std::memcpy(uuid.m_Bytes.data(), randomHalves, kByteCount);

		uuid.m_Bytes[kVersionByteIndex] = static_cast<std::uint8_t>(
			(uuid.m_Bytes[kVersionByteIndex] & kBitsBelowVersionMask) | kRandomVersionInTopNibble);
		uuid.m_Bytes[kVariantByteIndex] = static_cast<std::uint8_t>(
			(uuid.m_Bytes[kVariantByteIndex] & kBitsBelowVariantMask) | kRfc4122VariantInTopTwoBits);

		return uuid;
	}

	std::string Uuid::ToString() const
	{
		std::string text;
		text.reserve(kCanonicalTextLength);

		std::size_t byteIndex = 0;

		for (const std::size_t groupByteCount : kCanonicalGroupByteCounts)
		{
			if (!text.empty())
			{
				text += '-';
			}

			for (std::size_t byteInGroup = 0; byteInGroup < groupByteCount; ++byteInGroup)
			{
				text += std::format("{:02X}", m_Bytes[byteIndex]);
				++byteIndex;
			}
		}

		return text;
	}
}
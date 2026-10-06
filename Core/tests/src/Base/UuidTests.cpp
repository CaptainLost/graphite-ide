#include <doctest/doctest.h>

#include "Core/Base/Uuid.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <set>
#include <string>

namespace {
	constexpr std::size_t kCanonicalTextLength = 36;
	constexpr std::size_t kSeparatorPositions[] = {8, 13, 18, 23};

	constexpr std::size_t kVersionByteIndex = 6;
	constexpr std::uint8_t kTopNibbleMask = 0xF0;
	constexpr std::uint8_t kRandomVersionInTopNibble = 0x40;
	constexpr char kRandomVersionDigit = '4';

	constexpr std::size_t kVariantByteIndex = 8;
	constexpr std::uint8_t kTopTwoBitsMask = 0xC0;
	constexpr std::uint8_t kRfc4122VariantInTopTwoBits = 0x80;

	constexpr std::size_t kFirstDigitOfThirdGroup = 14;
	constexpr std::size_t kFirstDigitOfFourthGroup = 19;

	constexpr std::size_t kIdentifierCount = 1000;

	bool IsRfc4122VariantDigit(char digit)
	{
		return digit == '8' || digit == '9' || digit == 'A' || digit == 'B';
	}
}

TEST_CASE("A generated identifier has the canonical shape without the braces a format that wants them adds itself")
{
	const std::string text = Core::Uuid::Generate().ToString();

	REQUIRE(text.size() == kCanonicalTextLength);
	for (const std::size_t separatorPosition : kSeparatorPositions)
	{
		CAPTURE(separatorPosition);
		CHECK(text[separatorPosition] == '-');
	}

	CHECK(text.front() != '{');
	CHECK(text.back() != '}');
}

TEST_CASE("Every character is a hexadecimal digit in uppercase as Visual Studio writes them or a separator")
{
	const std::string text = Core::Uuid::Generate().ToString();

	for (const char character : text)
	{
		if (character == '-')
		{
			continue;
		}

		const bool isDecimalDigit = character >= '0' && character <= '9';
		const bool isUppercaseHexLetter = character >= 'A' && character <= 'F';

		CHECK((isDecimalDigit || isUppercaseHexLetter));
	}
}

TEST_CASE("The version and variant bits say what kind of identifier this is")
{
	const Core::Uuid uuid = Core::Uuid::Generate();
	const std::array<std::uint8_t, Core::Uuid::kByteCount>& bytes = uuid.GetBytes();

	CHECK((bytes[kVersionByteIndex] & kTopNibbleMask) == kRandomVersionInTopNibble);
	CHECK((bytes[kVariantByteIndex] & kTopTwoBitsMask) == kRfc4122VariantInTopTwoBits);
}

TEST_CASE("The text says the same thing the bytes do so a grouping mistake in ToString shows")
{
	const Core::Uuid uuid = Core::Uuid::Generate();
	const std::string text = uuid.ToString();

	const char versionDigit = text[kFirstDigitOfThirdGroup];
	CHECK(versionDigit == kRandomVersionDigit);

	const char variantDigit = text[kFirstDigitOfFourthGroup];
	CHECK(IsRfc4122VariantDigit(variantDigit));
}

TEST_CASE("A thousand identifiers in a row are all different where an engine seeded afresh on each call from a fixed value or a coarse clock would repeat")
{
	std::set<std::string> seen;

	for (std::size_t generatedCount = 0; generatedCount < kIdentifierCount; ++generatedCount)
	{
		seen.insert(Core::Uuid::Generate().ToString());
	}

	CHECK(seen.size() == kIdentifierCount);
}

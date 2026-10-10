#include <doctest/doctest.h>

#include "Core/Base/ByteText.h"

#include <cstdint>
#include <optional>
#include <ostream>
#include <vector>

TEST_CASE("Bytes written as hex read back unchanged")
{
	const std::vector<std::uint8_t> bytes{0x00, 0x0F, 0xA5, 0xFF};

	CHECK(Core::BytesToHex(bytes) == "000fa5ff");
	CHECK(Core::BytesFromHex("000fa5ff") == bytes);
	CHECK(Core::BytesFromHex("000FA5FF") == bytes);
	CHECK(Core::BytesFromHex("") == std::vector<std::uint8_t>{});
}

TEST_CASE("Hex that is odd in length or holds another character reads as nothing")
{
	CHECK_FALSE(Core::BytesFromHex("abc").has_value());
	CHECK_FALSE(Core::BytesFromHex("zz").has_value());
	CHECK_FALSE(Core::BytesFromHex("0x12").has_value());
}

TEST_CASE("A base64 SHA-512 digest reads back as its 64 bytes")
{
	const std::optional<std::vector<std::uint8_t>> digest = Core::BytesFromBase64(
		"oTi5yPnendY0PpsNDI9NUWN7un44YGt8Kwix2R6O76OfEhIwV/ZiX0JYErQ2HlKEG9KFZwTsEn/1AUTOBRcKmQ==");

	REQUIRE(digest.has_value());
	CHECK(digest->size() == 64);
	CHECK(Core::BytesToHex(*digest) ==
		  "a138b9c8f9de9dd6343e9b0d0c8f4d51637bba7e38606b7c2b08b1d91e8eefa3"
		  "9f12123057f6625f425812b4361e52841bd2856704ec127ff50144ce05170a99");
}

TEST_CASE("Base64 with each amount of padding reads back")
{
	CHECK(Core::BytesFromBase64("TWFu") == std::vector<std::uint8_t>{'M', 'a', 'n'});
	CHECK(Core::BytesFromBase64("TWE=") == std::vector<std::uint8_t>{'M', 'a'});
	CHECK(Core::BytesFromBase64("TQ==") == std::vector<std::uint8_t>{'M'});
	CHECK(Core::BytesFromBase64("") == std::vector<std::uint8_t>{});
}

TEST_CASE("Base64 of the wrong length or with a stray character reads as nothing")
{
	CHECK_FALSE(Core::BytesFromBase64("TWF").has_value());
	CHECK_FALSE(Core::BytesFromBase64("TW=u").has_value());
	CHECK_FALSE(Core::BytesFromBase64("TW u").has_value());
	CHECK_FALSE(Core::BytesFromBase64("====").has_value());
	CHECK_FALSE(Core::BytesFromBase64("TWFu-_==").has_value());
}

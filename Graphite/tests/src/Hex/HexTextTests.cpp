#include <doctest/doctest.h>

#include "Hex/HexText.h"

#include <array>

TEST_CASE("Rows hold sixteen bytes and the last one may be short")
{
	CHECK(Graphite::RowCount(0) == 0);
	CHECK(Graphite::RowCount(1) == 1);
	CHECK(Graphite::RowCount(16) == 1);
	CHECK(Graphite::RowCount(17) == 2);
}

TEST_CASE("Offsets have eight digits until the file reaches 4 GiB")
{
	constexpr std::uint64_t kGiB = 1024ull * 1024 * 1024;
	CHECK(Graphite::OffsetDigits(1) == 8);
	CHECK(Graphite::OffsetDigits(4 * kGiB) == 8);
	CHECK(Graphite::OffsetDigits(4 * kGiB + 1) == 9);
	CHECK(Graphite::OffsetDigits(5 * kGiB) == 9);
}

TEST_CASE("Offsets and bytes are upper-case hex")
{
	CHECK(Graphite::OffsetText(0x4E, 8) == "0000004E");
	CHECK(Graphite::OffsetText(0x1234ABCDE, 9) == "1234ABCDE");
	CHECK(Graphite::ByteText(0x5A) == "5A");
	CHECK(Graphite::ByteText(0x0F) == "0F");
}

TEST_CASE("Printable bytes show as themselves and others as dots")
{
	CHECK(Graphite::AsciiOf('T') == 'T');
	CHECK(Graphite::AsciiOf(0x20) == ' ');
	CHECK(Graphite::AsciiOf(0x7E) == '~');
	CHECK(Graphite::AsciiOf(0x7F) == '.');
	CHECK(Graphite::AsciiOf(0x00) == '.');
	CHECK(Graphite::AsciiOf(0xC4) == '.');
}

TEST_CASE("Zero bytes and dot stand-ins are dim")
{
	CHECK(Graphite::IsDim(0));
	CHECK_FALSE(Graphite::IsDim('A'));
	CHECK_FALSE(Graphite::IsDim(0x20));
	CHECK(Graphite::IsDim(0x7F));
	CHECK(Graphite::IsDim(0xFF));
}

TEST_CASE("Copied bytes are hex pairs separated by spaces")
{
	const std::array<std::uint8_t, 4> bytes{0x4D, 0x5A, 0x90, 0x00};
	CHECK(Graphite::CopyText(bytes) == "4D 5A 90 00");
	CHECK(Graphite::CopyText({}) == "");
}

TEST_CASE("The status bar names the offset, the count, the values and the size")
{
	const std::array<std::uint8_t, 4> bytes{'T', 'h', 'i', 's'};
	const auto items = Graphite::HexStatusItems(0x4E, 12, bytes, 4382720);

	REQUIRE(items.size() == 4);
	CHECK(items[0].Text == "Offset 0x0000004E");
	CHECK(items[1].Text == "12 bytes");
	CHECK(items[2].Text == "u8 84  u16 26708  u32 1936287828");
	CHECK(items[2].Code);
	CHECK_FALSE(items[0].Code);
	CHECK(items[3].Text == "4.2 MB");
}

TEST_CASE("A single selected byte is named in the singular")
{
	const std::array<std::uint8_t, 1> bytes{'T'};
	const auto items = Graphite::HexStatusItems(0, 1, bytes, 100);

	REQUIRE(items.size() == 4);
	CHECK(items[1].Text == "1 byte");
}

TEST_CASE("A value needing more bytes than the file has is left out")
{
	const std::array<std::uint8_t, 1> bytes{0x4D};
	const auto items = Graphite::HexStatusItems(0, 1, bytes, 1);

	REQUIRE(items.size() == 4);
	CHECK(items[2].Text == "u8 77");
}

TEST_CASE("Three bytes left show the byte and the short value only")
{
	const std::array<std::uint8_t, 3> bytes{0x01, 0x02, 0x03};
	const auto items = Graphite::HexStatusItems(5, 1, bytes, 8);

	REQUIRE(items.size() == 4);
	CHECK(items[2].Text == "u8 1  u16 513");
}

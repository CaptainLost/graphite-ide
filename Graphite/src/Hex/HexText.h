#pragma once

#include "Editor/StatusBar/StatusBar.h"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Graphite {

	inline constexpr std::uint64_t kBytesPerRow = 16;

	std::uint64_t RowCount(std::uint64_t fileSize);

	// Eight digits, or as many as the last offset needs when that is more.
	int OffsetDigits(std::uint64_t fileSize);

	std::string OffsetText(std::uint64_t offset, int digits);

	// Two upper-case hex digits; the view points into static storage.
	std::string_view ByteText(std::uint8_t b);

	// The character for 0x20-0x7E, a dot for every other byte.
	char AsciiOf(std::uint8_t b);

	std::string CopyText(std::span<const std::uint8_t> bytes);

	// `fromFirst` holds up to four bytes starting at `first`; a value is shown only when it has enough of them.
	std::vector<ViewStatusItem> HexStatusItems(std::uint64_t first, std::uint64_t count,
											   std::span<const std::uint8_t> fromFirst, std::uint64_t fileSize);
}

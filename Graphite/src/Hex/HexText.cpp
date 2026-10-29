#include "Hex/HexText.h"

#include "Core/Base/ByteSize.h"
#include "Core/Base/Count.h"

#include <array>
#include <format>

namespace Graphite {

	namespace {

		constexpr char kDigits[] = "0123456789ABCDEF";

		constexpr std::array<char, 512> MakeByteTable()
		{
			std::array<char, 512> table{};
			for (int i = 0; i < 256; ++i)
			{
				table[static_cast<std::size_t>(i) * 2] = kDigits[i >> 4];
				table[static_cast<std::size_t>(i) * 2 + 1] = kDigits[i & 15];
			}
			return table;
		}

		constexpr std::array<char, 512> kByteTable = MakeByteTable();

		std::uint64_t LittleEndian(std::span<const std::uint8_t> bytes, std::size_t width)
		{
			std::uint64_t value = 0;
			for (std::size_t i = 0; i < width; ++i)
			{
				value |= static_cast<std::uint64_t>(bytes[i]) << (8 * i);
			}
			return value;
		}

	} // namespace

	std::uint64_t RowCount(std::uint64_t fileSize)
	{
		return fileSize / kBytesPerRow + (fileSize % kBytesPerRow != 0 ? 1 : 0);
	}

	int OffsetDigits(std::uint64_t fileSize)
	{
		int digits = 1;
		for (std::uint64_t last = fileSize > 0 ? fileSize - 1 : 0; last >= 16; last >>= 4)
		{
			++digits;
		}
		return digits > 8 ? digits : 8;
	}

	std::string OffsetText(std::uint64_t offset, int digits)
	{
		return std::format("{:0{}X}", offset, digits);
	}

	std::string_view ByteText(std::uint8_t b)
	{
		return {kByteTable.data() + static_cast<std::size_t>(b) * 2, 2};
	}

	char AsciiOf(std::uint8_t b)
	{
		return b >= 0x20 && b <= 0x7E ? static_cast<char>(b) : '.';
	}

	std::string CopyText(std::span<const std::uint8_t> bytes)
	{
		std::string text;
		text.reserve(bytes.size() * 3);
		for (const std::uint8_t b : bytes)
		{
			if (!text.empty())
			{
				text += ' ';
			}
			text += ByteText(b);
		}
		return text;
	}

	std::vector<ViewStatusItem> HexStatusItems(std::uint64_t first, std::uint64_t count,
											   std::span<const std::uint8_t> fromFirst, std::uint64_t fileSize)
	{
		std::vector<ViewStatusItem> items;
		items.push_back({.Text = "0x" + OffsetText(first, OffsetDigits(fileSize)), .Code = true, .Label = "Offset "});
		items.push_back({.Text = count == 1 ? std::string("1 byte") : Core::FormatCount(count) + " bytes"});

		std::string values;
		const auto add = [&](const char* name, std::size_t width) {
			if (fromFirst.size() >= width)
			{
				if (!values.empty())
				{
					values += "  ";
				}
				values += std::format("{} {}", name, LittleEndian(fromFirst, width));
			}
		};
		add("u8", 1);
		add("u16", 2);
		add("u32", 4);
		items.push_back({std::move(values), true, std::nullopt});

		items.push_back({Core::FormatByteSize(fileSize), false, std::nullopt});
		return items;
	}
}

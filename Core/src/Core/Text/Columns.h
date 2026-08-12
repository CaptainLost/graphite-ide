#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace Core {

	class TextBuffer;

	inline constexpr std::size_t DefaultTabWidth = 4;

	std::size_t ColumnAfterTab(std::size_t column, std::size_t tabWidth);

	std::size_t ColumnOfByte(std::string_view line, std::size_t byte,
							 std::size_t tabWidth);

	std::size_t ByteOfColumn(std::string_view line, std::size_t column,
							 std::size_t tabWidth);

	std::size_t ColumnCount(std::string_view line, std::size_t tabWidth);

	std::string ExpandTabs(std::string_view line, std::size_t tabWidth);

	std::size_t WidestColumn(const TextBuffer& buffer, std::size_t tabWidth);
}
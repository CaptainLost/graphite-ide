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

	// Where byte `byte` of `line` lands in the string ExpandTabs would produce from it.
	//
	// Deliberately not ColumnOfByte, which counts one column per character: the expanded string
	// advances by a character's byte length, so the two agree only while a line is pure ASCII.
	// Anything holding a position in the raw line and needing to find it among tokens a lexer
	// produced - those being offsets into the expanded text - has to come through here.
	std::size_t ExpandedOffsetOfByte(std::string_view line, std::size_t byte,
									 std::size_t tabWidth);

	std::size_t WidestColumn(const TextBuffer& buffer, std::size_t tabWidth);
}
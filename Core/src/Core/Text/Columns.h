#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace Core {

	class CancellationToken;
	class TextBuffer;

	inline constexpr std::size_t DefaultTabWidth = 4;

	std::size_t ColumnAfterTab(std::size_t column, std::size_t tabWidth);

	std::size_t ColumnOfByte(std::string_view lineText, std::size_t byteOffset,
							 std::size_t tabWidth);

	std::size_t ByteOfColumn(std::string_view lineText, std::size_t column,
							 std::size_t tabWidth);

	std::size_t ColumnCount(std::string_view lineText, std::size_t tabWidth);

	std::size_t ColumnAfterText(std::string_view text, std::size_t startColumn, std::size_t tabWidth);

	std::optional<std::size_t> ColumnCountUnlessCancelled(std::string_view lineText, std::size_t tabWidth, const CancellationToken& cancellation);

	std::string ExpandTabs(std::string_view lineText, std::size_t tabWidth);

	// Not ColumnOfByte: that counts one column per character, while the text ExpandTabs produces
	// advances by each character's byte length, so the two agree only on pure ASCII. A position
	// matched against a lexer's tokens, which are offsets into that text, has to come through here.
	std::size_t ExpandedOffsetOfByte(std::string_view lineText, std::size_t byteOffset,
									 std::size_t tabWidth);

	std::size_t WidestColumn(const TextBuffer& buffer, std::size_t tabWidth);
}